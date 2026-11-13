/**
 * Catalog of the extensions shipped with xbintsc.
 *
 * Callers (the CLI, embedding tools) use this to hint the compiler about
 * modules that *would* be available if an extension were enabled, so an import
 * of a known module without its `--ext` flag fails with an actionable message
 * (`pass --ext node`) rather than a confusing downstream error.
 *
 * The core compiler stays platform agnostic: it only sees the resulting hint
 * table, never the Node extension itself.
 */

import type { Extension } from "./registry.js";
import { nodeExtension } from "./node/index.js";
import { guiExtension } from "./gui/index.js";
import { webworkerExtension } from "./webworker/index.js";

/** Every extension bundled with xbintsc, in registration order. */
export function bundledExtensions(): readonly Extension[] {
  /* `webworker` is registered after `gui`: both define `self`, and the worker
   * extension's `self` must resolve to the worker scope (a page still reaches
   * the window through `window`). */
  return [nodeExtension, guiExtension, webworkerExtension];
}
