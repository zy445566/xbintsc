# Source-level module bundler

xbintsc has no runtime module system and no module linker. The driver resolves
every reachable module, renames its top-level bindings to globally unique
`<prefix><name>` identifiers, rewrites imports to those final names, and
concatenates the whole graph into one source file for codegen.

The public entry point is `bundleModules()` in `../modules.ts`; everything in
this directory is an implementation detail of it.

## Pipeline

| Phase | Location | Work |
| --- | --- | --- |
| 1 | `merge.ts` | Record top-level bindings and pick a unique final name per symbol. |
| 1b | `commonjs.ts` | Give CommonJS `node_modules` files synthetic `module` / `exports` state. |
| 2 | `merge.ts` | Fill every module's `exports` map from declarations and re-exports. |
| 2b | `commonjs.ts` | Lower `require("…")` to the already-bundled dependency's exports. |
| 3 | `merge.ts` | Rewrite imported names; lower deferred imports and namespace bindings. |
| 3b | `reexport-resolution.ts` | Re-resolve re-export chains to a fixpoint; rebuild eager namespace objects. |
| 4 | `concatenate.ts` | Emit statements in dependency order, dropping local imports and export wrappers. |
| 5 | `concatenate.ts` | Apply the final rename to each top-level declaration and reference. |

Phases 4-5 live in `concatenate.ts` so `merge.ts` stays focused on name and
export resolution.

## Files

| File | Responsibility |
| --- | --- |
| `types.ts` | `ModuleRecord`, `BundleResult`, resolution types. |
| `resolve.ts` | Specifier resolution: relative paths, directory indexes, package `exports`, `node_modules`. |
| `graph.ts` | Load and bind the import graph in dependency order. |
| `symbols.ts` | Top-level symbol lookup, declaration/reference renaming, export-map remapping. |
| `merge.ts` | Orchestrates phases 1-3 and produces the resolved module records. |
| `reexport-resolution.ts` | Fixpoint over `export … from` chains; rebuilds namespace objects from final names. |
| `namespace-object.ts` | Synthesizes `const ns = { name: finalName, … }` for namespace imports/re-exports. |
| `deferred.ts` | `<script src>` deferred evaluation: wrapper functions, alias lowering, deferred-set computation. |
| `dynamic-import.ts` | Lowers string-literal `import()` to a promise over the target's wrapper. |
| `commonjs.ts` | CommonJS interop for `node_modules` sources. |
| `concatenate.ts` | Phases 4-5: emit the merged statement list and rename declarations. |

## Invariants

- **No live bindings.** An imported name is a snapshot of the exported value at
  module-evaluation time.
- **The entry module is last** in the record order; dependencies are
  post-order.
- **Deferred modules own their bindings.** A deferred module's top-level names
  are local to its `__xt_deferred_*` wrapper. A namespace binding between two
  deferred modules must stay an alias that reads the dependency's wrapper
  (`deferred.ts`); it must not be rebuilt at top level from wrapper-local names
  (TS3002). Regression coverage: `tests/driver/deferred-reexport.test.ts`.
- **Re-exports are settled before concatenation.** Phase 3b runs to a fixpoint
  so multi-level `export … from` chains and cycle back-edges see final names.
