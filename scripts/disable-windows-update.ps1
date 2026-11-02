#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Turn Windows Update off on a machine that runs long CI jobs.

.DESCRIPTION
  Windows Update is a plausible cause of a Windows job that stalls for minutes
  and then never finishes: the scan, download and install work happens in
  services and scheduled tasks the job never sees (`TiWorker`,
  `MoUsoCoreWorker`, `USO_UxBroker`), it can occupy every core for a while, and
  when an install finishes the machine reboots on its own. On a hosted runner
  that is a step that hangs until the job times out; on a self-hosted runner it
  is an offline runner and a cancelled job.

  This script makes the machine deterministic while the job runs:

    * local policy  - no automatic download, install or reboot
    * services      - wuauserv, UsoSvc, WaaSMedicSvc and DoSvc disabled
    * tasks         - UpdateOrchestrator, WindowsUpdate and InstallService
                      scheduled tasks disabled

  It is idempotent and best effort. A value the account may not write (some
  images protect WaaSMedicSvc with TrustedInstaller) is listed at the end and
  the script carries on, so a locked-down self-hosted runner still runs its
  job; pass -Strict to turn any such failure into exit code 1. Nothing is
  changed when the script is not elevated: it prints a warning instead.

  On a self-hosted runner the change persists across jobs. Run the script once
  with -Restore, from an elevated prompt, to put Windows Update back the way
  Windows installs it. Hosted runners are discarded after the job, so nothing
  needs undoing there.

  GitHub Actions calls it through `.github/actions/disable-windows-update`,
  which every Windows job invokes before its build starts.

.PARAMETER Restore
  Undo the change: restore the stock service start types, delete the policy
  values this script writes and re-enable the scheduled tasks.

.PARAMETER Strict
  Exit with code 1 when any setting could not be applied.

.EXAMPLE
  # Show what would change without changing anything.
  pwsh -File scripts/disable-windows-update.ps1 -WhatIf

.EXAMPLE
  # Undo it on a self-hosted runner (elevated prompt).
  pwsh -File scripts/disable-windows-update.ps1 -Restore
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [switch]$Restore,
  [switch]$Strict
)

$ErrorActionPreference = "Stop"

# The start types a stock Windows install uses, so -Restore can put the
# services back: 2 = Automatic, 3 = Manual (trigger start), 4 = Disabled.
$defaultStart = [ordered]@{
  wuauserv     = 3
  UsoSvc       = 2
  WaaSMedicSvc = 3
  DoSvc        = 2
}

# Task folders owned by Windows Update and its install/reboot agents.
$taskPrefixes = @(
  "\Microsoft\Windows\UpdateOrchestrator\"
  "\Microsoft\Windows\WindowsUpdate\"
  "\Microsoft\Windows\InstallService\"
)

$wuPolicyKey = "HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate"
$auPolicyKey = "$wuPolicyKey\AU"

# NoAutoUpdate stops the scan/download/install cycle. AUOptions=2 keeps the
# machine on "notify" for an administrator who updates by hand. The reboot
# value is what protects a long job from disappearing mid-step, and minor
# updates are pinned off so a driver cannot slip in unattended.
$policyValues = [ordered]@{
  NoAutoUpdate                  = 1
  AUOptions                     = 2
  NoAutoRebootWithLoggedOnUsers = 1
  AutoInstallMinorUpdates       = 0
}

$whatIf = [bool]$WhatIfPreference
$problems = [System.Collections.Generic.List[string]]::new()

function Add-Problem([string]$message) {
  $script:problems.Add($message)
}

function Set-ServiceStart([string]$name, [int]$start) {
  $key = "HKLM:\SYSTEM\CurrentControlSet\Services\$name"
  if (-not (Test-Path $key)) {
    Add-Problem "service $name is not installed"
    return
  }
  if ($whatIf) {
    Write-Host "  would set ${name} start type to $start"
    return
  }
  try {
    New-ItemProperty -Path $key -Name Start -Value $start -PropertyType DWord -Force | Out-Null
  } catch {
    Add-Problem "service ${name}: $($_.Exception.Message)"
  }
}

function Set-PolicyValue([string]$name, [int]$value) {
  if ($whatIf) {
    Write-Host "  would set $name = $value under $auPolicyKey"
    return
  }
  try {
    # The WindowsUpdate policy key does not exist on a stock machine, and the
    # AU subkey only appears once a policy has been set, so create both.
    foreach ($key in @($wuPolicyKey, $auPolicyKey)) {
      if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    }
    New-ItemProperty -Path $auPolicyKey -Name $name -Value $value -PropertyType DWord -Force | Out-Null
  } catch {
    Add-Problem "policy ${name}: $($_.Exception.Message)"
  }
}

function Remove-PolicyValue([string]$name) {
  if ($whatIf) {
    Write-Host "  would remove policy $name"
    return
  }
  try {
    Remove-ItemProperty -Path $auPolicyKey -Name $name -ErrorAction SilentlyContinue
  } catch {
    Add-Problem "policy ${name}: $($_.Exception.Message)"
  }
}

function Stop-UpdateService([string]$name) {
  if ($whatIf) {
    Write-Host "  would stop $name"
    return
  }
  try {
    Stop-Service -Name $name -Force -ErrorAction Stop
  } catch {
    # A service that refused to stop is not fatal: its start type is disabled
    # separately, which is what keeps it from coming back on its own.
    Write-Verbose "could not stop ${name}: $($_.Exception.Message)"
  }
}

function Set-UpdateTasks([bool]$disabled) {
  $tasks = @(Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object {
      $taskPath = $_.TaskPath
      foreach ($prefix in $taskPrefixes) {
        if ($taskPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
          return $true
        }
      }
      return $false
    })

  foreach ($task in $tasks) {
    $label = "$($task.TaskPath)$($task.TaskName)"
    if ($whatIf) {
      $verb = if ($disabled) { "disable" } else { "enable" }
      Write-Host "  would $verb task $label"
      continue
    }
    try {
      if ($disabled) {
        Disable-ScheduledTask -TaskName $task.TaskName -TaskPath $task.TaskPath -ErrorAction Stop | Out-Null
      } else {
        Enable-ScheduledTask -TaskName $task.TaskName -TaskPath $task.TaskPath -ErrorAction Stop | Out-Null
      }
    } catch {
      Add-Problem "scheduled task ${label}: $($_.Exception.Message)"
    }
  }
}

# -WhatIf is a read-only preview, so it stays available without elevation;
# actually changing the machine needs an administrator account.
if (-not $whatIf) {
  $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Not elevated: Windows Update was left unchanged. Run this from an administrator prompt, or run the CI runner service as an administrator, to apply the change."
    if ($env:GITHUB_ACTIONS) {
      Write-Host "::warning::Windows Update was not disabled: the runner is not elevated."
    }
    if ($Strict) { exit 1 }
    exit 0
  }
}

if ($Restore) {
  Write-Host "Restoring Windows Update defaults"
  foreach ($name in $defaultStart.Keys) { Set-ServiceStart $name $defaultStart[$name] }
  foreach ($name in $policyValues.Keys) { Remove-PolicyValue $name }
  Set-UpdateTasks $false
} else {
  Write-Host "Disabling Windows Update"
  foreach ($name in $policyValues.Keys) { Set-PolicyValue $name $policyValues[$name] }
  # Disable the start types before stopping anything, so a trigger-started
  # service cannot come back while the others are being stopped.
  foreach ($name in $defaultStart.Keys) { Set-ServiceStart $name 4 }
  foreach ($name in $defaultStart.Keys) { Stop-UpdateService $name }
  Set-UpdateTasks $true
}

if ($problems.Count -gt 0) {
  Write-Host ""
  Write-Host "Could not apply $($problems.Count) setting(s); the rest were applied:"
  foreach ($problem in $problems) { Write-Host "  - $problem" }
  if ($env:GITHUB_ACTIONS) {
    Write-Host "::warning::$($problems.Count) Windows Update setting(s) could not be changed; see the step log above."
  }
  if ($Strict) { exit 1 }
}

if (-not $whatIf) {
  Write-Host ""
  Get-Service -Name @($defaultStart.Keys) -ErrorAction SilentlyContinue |
    Format-Table Name, Status, StartType -AutoSize
  Get-ItemProperty -Path $auPolicyKey -ErrorAction SilentlyContinue |
    Select-Object NoAutoUpdate, AUOptions, NoAutoRebootWithLoggedOnUsers |
    Format-List
}
