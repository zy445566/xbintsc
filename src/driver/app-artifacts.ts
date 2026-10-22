/**
 * Platform artifacts that turn a linked program into a desktop application:
 * the Windows-subystem entry shim, the embedded icon object and the Windows PE
 * icon resource.
 *
 * They are collected separately from the link call so icon/GUI/package work
 * has a home of its own instead of growing `build`.
 */

import type { ExtensionRegistry } from "../extensions/registry.js";
import { ensureIconObject, EMPTY_ICON, IconError, type IconInfo } from "./icon.js";
import { ensureWindowsEntryObject } from "./win-entry.js";
import { ensureWindowsIconResource, resolveResourceCompiler } from "./win-icon.js";
import type { AppConfig } from "./config.js";
import type { Runner } from "./toolchain.js";

export interface AppArtifactRequest {
  readonly runner: Runner;
  readonly clang: string;
  readonly cacheDir: string;
  readonly registry: ExtensionRegistry;
  readonly app: AppConfig;
  readonly icon: IconInfo | undefined;
  readonly env: Record<string, string>;
  readonly verbose: boolean | undefined;
}

export interface AppArtifacts {
  /** Windows-subsystem entry shim, when the program is a GUI app. */
  readonly entryObject?: string;
  readonly iconObjects: readonly string[];
  /** Extra linker flags implied by the subsystem (empty on other platforms). */
  readonly linkerFlags: readonly string[];
  /** Set when the icon could not be embedded; the caller reports it. */
  readonly iconError?: string;
}

/**
 * Build the objects (and flags) that accompany the program object.
 *
 * On Windows a GUI program links as a **Windows-subsystem** executable, so
 * launching it does not flash a console window. That subsystem's C runtime
 * wants `WinMain`, so a tiny shim object bridges it to the runtime's `main`.
 * `app.console: true` keeps the console subsystem instead (useful while
 * debugging, because stdout and stderr only exist there). Other platforms have
 * no equivalent.
 */
export function ensureAppArtifacts(request: AppArtifactRequest): AppArtifacts {
  const { runner, clang, cacheDir, registry, app, icon, env, verbose } = request;
  const windowsSubsystem = process.platform === "win32" && registry.has("gui") && app.console !== true;
  const linkerFlags = windowsSubsystem ? ["-Wl,/SUBSYSTEM:WINDOWS"] : [];
  const entryObject = windowsSubsystem
    ? ensureWindowsEntryObject(runner, clang, cacheDir, env)
    : undefined;

  const iconObjects: string[] = [];
  // The GUI extension references the icon symbols unconditionally, so a GUI
  // program gets an (empty) icon object even with no icon configured.
  if (icon !== undefined || registry.has("gui")) {
    try {
      iconObjects.push(ensureIconObject(runner, clang, cacheDir, icon ?? EMPTY_ICON, env));
      if (icon && process.platform === "win32") {
        const resourceCompiler = resolveResourceCompiler(runner, clang);
        if (resourceCompiler) {
          iconObjects.push(ensureWindowsIconResource(runner, cacheDir, icon, resourceCompiler, env));
        } else if (verbose) {
          process.stderr.write("xbintsc: no llvm-rc/windres found; skipping the embedded .exe icon\n");
        }
      }
    } catch (error) {
      if (error instanceof IconError) return { entryObject, iconObjects, linkerFlags, iconError: error.message };
      throw error;
    }
  }

  return { entryObject, iconObjects, linkerFlags };
}
