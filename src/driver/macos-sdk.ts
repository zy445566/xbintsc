/**
 * macOS SDK discovery.
 *
 * macOS has no `/usr/include`: every C standard header (`stdio.h`,
 * `setjmp.h`, …) ships inside the SDK. clang only searches it when it has a
 * sysroot, which comes from the `/usr/bin/clang` shim, an explicit
 * `-isysroot`, or the `SDKROOT` environment variable.
 *
 * xbintsc invokes clang by absolute path (CI pins it to the oldest installed
 * Xcode so released archives stay linkable by newer ones), which bypasses the
 * shim. Without a sysroot that driver sees neither the SDK nor `/usr/include`,
 * and the first runtime include fails with `'setjmp.h' file not found` — the
 * header is irrelevant, any system header would do.
 *
 * Asking `xcrun` once and handing `SDKROOT` to the toolchain subprocesses
 * covers every C compile the driver performs — the runtime, its extensions and
 * the icon helpers — and the link that follows them. An explicit `SDKROOT` in
 * the environment always wins, so a developer can select an older SDK
 * deliberately.
 */

import { existsSync } from "node:fs";
import type { CommandResult, Runner } from "./toolchain.js";

/**
 * The macOS SDK `xcrun` reports, or `undefined` when the host is not macOS,
 * the environment already names one, or no usable SDK can be resolved.
 *
 * A missing SDK is not an error here: the build still runs, and clang reports
 * the real problem (or the driver never needed the headers in the first place).
 */
export function macosSdkRoot(
  runner: Runner,
  env: NodeJS.ProcessEnv = process.env,
  platform: NodeJS.Platform = process.platform,
): string | undefined {
  if (platform !== "darwin") return undefined;
  if (env.SDKROOT) return undefined;
  let result: CommandResult;
  try {
    result = runner.run("xcrun", ["--show-sdk-path"]);
  } catch {
    /* A host without xcrun keeps the previous behaviour: let clang decide. */
    return undefined;
  }
  if (result.status !== 0) return undefined;
  const sdk = result.stdout.trim();
  return sdk.length > 0 && existsSync(sdk) ? sdk : undefined;
}

/** Extra environment for clang subprocesses: `{ SDKROOT }` on macOS only. */
export function toolchainEnvironment(
  runner: Runner,
  env: NodeJS.ProcessEnv = process.env,
  platform: NodeJS.Platform = process.platform,
): Record<string, string> {
  const sdk = macosSdkRoot(runner, env, platform);
  return sdk === undefined ? {} : { SDKROOT: sdk };
}
