/**
 * macOS SDK discovery.
 *
 * macOS has no `/usr/include`: every C standard header (`stdio.h`,
 * `setjmp.h`, …) ships inside the SDK, and the linker's `libm`/`libSystem`
 * stubs live there too. clang only searches the SDK when it has a sysroot,
 * which comes from the `/usr/bin/clang` shim, an explicit `-isysroot`, or the
 * `SDKROOT` environment variable.
 *
 * xbintsc invokes clang by absolute path (CI pins it to the oldest installed
 * Xcode so released archives stay linkable by newer ones), which bypasses the
 * shim. Without a sysroot that driver sees neither the SDK nor `/usr/include`,
 * and the first runtime include fails with `'setjmp.h' file not found` — the
 * header is irrelevant, any system header would do. The same gap makes the
 * link fail later with `ld: library 'm' not found`.
 *
 * The SDK is therefore resolved once, with `xcrun`, and handed to clang as
 * `-isysroot` on the command line rather than as `SDKROOT` in the environment:
 * a self-hosted xbintsc binary runs the same driver, and its compiled
 * `spawnSync` ignores `options.env`, so the variable would be lost exactly
 * where the compiler rebuilds itself. An explicit `SDKROOT` in the environment
 * always wins, so a developer can select an older SDK deliberately.
 */

import { existsSync } from "node:fs";
import type { CommandResult, Runner } from "./toolchain.js";

/**
 * The macOS SDK `xcrun` reports, or `undefined` when the host is not macOS,
 * the environment already names one, or no usable SDK can be resolved.
 *
 * A missing SDK is not an error here: the build still runs, and clang reports
 * the real problem (or the driver never needed the SDK in the first place).
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
