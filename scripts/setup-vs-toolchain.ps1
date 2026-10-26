#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Select the Visual Studio MSVC + LLVM toolchain for a Windows build.

.DESCRIPTION
  Finds the Visual Studio installation that owns the C++ build tools, imports
  its `vcvarsall` environment (INCLUDE, LIB, PATH, ...), verifies the LLVM
  bundled with that same installation (`clang`, `clang++`, `clang-cl`,
  `llvm-ar`, `llvm-rc`) and exposes it as `xbintsc_CLANG`.

  This is the toolchain a local build resolves and the one the prebuilt
  `gui.lib` / `media.lib` archives are compiled with. GitHub Actions calls this
  script from `.github/actions/vs-toolchain` instead of installing a separate
  LLVM, so CI and local builds share one compiler chain.

  Run it directly from a PowerShell prompt to use the same environment
  interactively:

      pwsh -File scripts/setup-vs-toolchain.ps1

  In GitHub Actions it also appends the environment to `GITHUB_ENV` and the
  LLVM bin directory to `GITHUB_PATH`, so later steps see it.

.PARAMETER InstallMake
  Install GNU make through Chocolatey (the FFmpeg media build needs it). Only
  used by CI; a local developer already has a native GNU make on PATH.
#>
[CmdletBinding()]
param(
  [switch]$InstallMake
)

$ErrorActionPreference = "Stop"

function Write-GitHubEnv([string]$name, [string]$value) {
  if ($env:GITHUB_ENV) {
    "$name=$value" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
  }
}

function Write-GitHubPath([string]$path) {
  if ($env:GITHUB_PATH) {
    $path | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8
  }
}

# 1. Find Visual Studio with the C++ tools for this machine's architecture.
$arch = if ($env:RUNNER_ARCH -eq "ARM64" -or $env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "arm64" } else { "x64" }
$llvmArch = if ($arch -eq "arm64") { "ARM64" } else { "x64" }
$component = if ($arch -eq "arm64") {
  "Microsoft.VisualStudio.Component.VC.Tools.arm64"
} else {
  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
}

$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  $vswhere = Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe not found; install Visual Studio with the C++ build tools."
}

$vsPath = (& $vswhere -latest -products * -requires $component -property installationPath |
  Select-Object -First 1)
if ($vsPath) { $vsPath = $vsPath.Trim() }
if (-not $vsPath -or -not (Test-Path $vsPath)) {
  throw "Visual Studio C++ build tools ($component) were not found."
}

# 2. Import the vcvarsall environment into this process and, in CI, the rest of
#    the job. vcvarsall is locale-independent for KEY=VALUE output.
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvars)) { throw "vcvarsall.bat not found at $vcvars" }
foreach ($line in (cmd /c "`"$vcvars`" $arch && set")) {
  if ($line -match "^([^=]+)=(.*)$") {
    $name = $matches[1]
    $value = $matches[2]
    Set-Item -Path "Env:$name" -Value $value
    Write-GitHubEnv $name $value
  }
}

# 3. Use the LLVM bundled with that Visual Studio installation. This is the
#    same compiler the driver and the build scripts resolve locally; the
#    fallback handles installations that put LLVM at the VC\Tools\Llvm root.
$llvmBin = Join-Path $vsPath "VC\Tools\Llvm\$llvmArch\bin"
if (-not (Test-Path (Join-Path $llvmBin "clang.exe"))) {
  $llvmBin = Join-Path $vsPath "VC\Tools\Llvm\bin"
}
$clang = Join-Path $llvmBin "clang.exe"
if (-not (Test-Path $clang)) {
  throw @"
Visual Studio at $vsPath has no LLVM at $llvmBin.
Install the "C++ Clang tools for Windows" component (Microsoft.VisualStudio.Component.VC.Llvm.Clang).
"@
}

$versionOutput = & $clang --version
if ($LASTEXITCODE -ne 0) { throw "$clang --version failed." }
$versionLine = ($versionOutput | Select-Object -First 1).Trim()
if ($versionLine -notmatch "clang version (\d+)\.") {
  throw "$clang did not report a clang version: $versionLine"
}
$major = [int]$matches[1]
if ($major -lt 16) {
  throw "Visual Studio's clang is $versionLine; xbintsc requires clang 16 or newer."
}
$targetLine = $versionOutput | Where-Object { $_ -match "^Target:" } | Select-Object -First 1
if (-not $targetLine -or $targetLine -notmatch "windows-msvc") {
  throw "Visual Studio's clang targets '$targetLine', not the MSVC ABI."
}
$expectedTarget = if ($arch -eq "arm64") { "aarch64-pc-windows-msvc" } else { "x86_64-pc-windows-msvc" }
if ($targetLine -notmatch [regex]::Escape($expectedTarget)) {
  throw "Visual Studio's clang targets '$targetLine'; this runner is $arch (expected $expectedTarget)."
}

# 4. Put it in front of PATH and tell the driver exactly which clang to use.
$env:xbintsc_CLANG = $clang
$env:Path = "$llvmBin;$env:Path"
Write-GitHubEnv "xbintsc_CLANG" $clang
Write-GitHubPath $llvmBin

# 5. Optional: GNU make for FFmpeg's configure (media archive only).
if ($InstallMake) {
  if ($env:GITHUB_ACTIONS) {
    choco install make --no-progress -y
    if ($LASTEXITCODE -ne 0) { throw "choco install make failed" }
    $makePath = "C:\ProgramData\chocolatey\bin\make.exe"
    if (-not (Test-Path $makePath)) { throw "GNU make was not installed at $makePath" }
  } else {
    $makeCommand = Get-Command make -ErrorAction SilentlyContinue
    if (-not $makeCommand) {
      throw "GNU make not found; install it (choco install make) or run without -InstallMake."
    }
    $makePath = $makeCommand.Source
  }
  $env:xbintsc_MAKE = $makePath
  Write-GitHubEnv "xbintsc_MAKE" $makePath
  $makeVersion = (& $makePath --version | Select-Object -First 1).Trim()
  if ($makeVersion -notmatch "GNU Make") {
    throw "$makePath is not GNU make: $makeVersion"
  }
  Write-Host "make         : $makePath ($makeVersion)"
}

Write-Host "Visual Studio: $vsPath"
Write-Host "MSVC arch    : $arch"
Write-Host "clang        : $clang ($versionLine)"
Write-Host "target       : $targetLine"
