/**
 * Extension-set construction for the CLI.
 *
 * `buildRegistry` turns the `--ext` names into a registry; `normalizeExtensions`
 * keeps the pre-link-feature spelling `--ext media` working by expanding it to
 * `gui` plus a forced `media` feature. Both live here so `main.ts` stays under
 * the repository's per-file line budget.
 */

import { bundledExtensions } from "../extensions/catalog.js";
import { nativeExtensionFromManifest } from "../extensions/native.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";

/** Register the requested bundled extensions and every native manifest. */
export function buildRegistry(
  extNames: readonly string[],
  nativeManifests: readonly string[],
): ExtensionRegistry {
  const registry = createDefaultRegistry();
  const bundled = bundledExtensions();
  for (const name of extNames) {
    const extension = bundled.find((candidate) => candidate.name === name);
    if (!extension) throw new Error(`Unknown extension '${name}'`);
    registry.register(extension);
  }
  // Each manifest describes a pre-built C++ or Rust library.
  for (const manifest of nativeManifests) {
    registry.register(nativeExtensionFromManifest(manifest));
  }
  // Hint the extensions that ship with xbintsc but were not enabled, so a
  // missing `import` of a known module points at the flag that enables it
  // (`pass --ext node`) instead of failing later with a confusing error.
  for (const extension of bundled) {
    if (!registry.has(extension.name)) registry.hintExtension(extension);
  }
  return registry;
}

/** The part of the CLI IO a normalization warning needs. */
export interface ExtensionWarningSink {
  stderr(text: string): void;
}

/**
 * `--ext media` predates link features: media is now an optional half of `gui`
 * selected from the program. Keep the old spelling working for one release by
 * expanding it to `gui` plus a forced `media` feature.
 */
export function normalizeExtensions(
  extNames: readonly string[],
  io: ExtensionWarningSink,
): { extensions: string[]; features: string[] } {
  const extensions: string[] = [];
  const features: string[] = [];
  let deprecatedMedia = false;
  for (const name of extNames) {
    if (name === "media") {
      deprecatedMedia = true;
      if (!features.includes("media")) features.push("media");
      if (!extensions.includes("gui")) extensions.push("gui");
      continue;
    }
    if (!extensions.includes(name)) extensions.push(name);
  }
  if (deprecatedMedia) {
    io.stderr(
      "xbintsc: warning: '--ext media' is deprecated; media is auto-detected with --ext gui " +
        "(use --feature media to force it)\n",
    );
  }
  return { extensions, features };
}
