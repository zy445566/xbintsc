/**
 * Source-level module bundler.
 *
 * xbintsc does not run a linker for modules, so `import`/`export` are lowered
 * at the driver by merging every reachable module into a single source file.
 * Each module's top-level bindings are renamed to a globally unique name and
 * imported names are rewritten to the (renamed) exported binding.
 *
 * The implementation lives under `bundler/` (see `bundler/README.md`):
 *   resolve.ts             specifier/path/package resolution
 *   graph.ts               import-graph loading (parse + bind)
 *   merge.ts               bundling pass orchestrator (phases 1-3)
 *   reexport-resolution.ts re-export chain fixpoint + namespace objects
 *   namespace-object.ts    namespace object literal builder
 *   deferred.ts            `<script src>` deferred module wrappers
 *   dynamic-import.ts      `import()` lowering
 *   commonjs.ts            CommonJS compatibility in `node_modules`
 *   concatenate.ts         final concatenation (phases 4-5)
 *   symbols.ts             top-level symbol renaming
 *   types.ts               shared types
 *
 * This file remains the public entry point for the driver and tests.
 */

export { bundleModules } from "./bundler/merge.js";
export type { BundleResult } from "./bundler/types.js";
