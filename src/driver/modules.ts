/**
 * Source-level module bundler.
 *
 * xbintsc does not run a linker for modules, so `import`/`export` are lowered
 * at the driver by merging every reachable module into a single source file.
 * Each module's top-level bindings are renamed to a globally unique name and
 * imported names are rewritten to the (renamed) exported binding.
 *
 * The implementation lives under `bundler/` split by concern:
 *   resolve.ts  specifier/path/package resolution
 *   graph.ts    import-graph loading (parse + bind)
 *   symbols.ts  top-level symbol renaming
 *   merge.ts    the bundling passes themselves
 *
 * This file remains the public entry point for the driver and tests.
 */

export { bundleModules } from "./bundler/merge.js";
export type { BundleResult } from "./bundler/types.js";
