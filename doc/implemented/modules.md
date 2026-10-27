# Module system

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/modules.md)

Location: `src/parser/parser.ts` (syntax), `src/driver/modules.ts` (bundling),
`src/driver/frontend.ts` (`parseEntry`).

## Syntax

- `import default, { named } from "..."`, `import * as ns from "..."` (namespace imports of relative modules are lowered to a synthetic object literal; extension modules such as `path` are also supported), `import "./side-effect"`, `import type`
- `export default`, `export { a as b }`, `export *`, `export =`
- Import attributes (`with` / `assert`)

## Runtime semantics

- The **runtime semantics** of `import` / `export` are handled at the driver layer by `src/driver/modules.ts`: it recursively resolves relative dependencies *and* bare `node_modules` packages (following `exports` / `module` / `main` and package subpaths), renames top-level symbols with a per-module prefix, rewrites references, merges into a single file and rebinds.
- Packages may be ESM or CommonJS: a CommonJS file under `node_modules` gets a synthetic `module`/`exports` pair, its `require` calls are lowered to the bundled dependency's exports, and its statically-detected exports are exposed to ESM importers. `require()` outside `node_modules` is still rejected with a hint to use `import`. Circular dependencies error out.
- A `.js`-family specifier resolves to its `.ts` source, and TypeScript suffixes are preferred over JavaScript siblings when the specifier omits the extension; plain `.js` / `.jsx` / `.mjs` / `.cjs` sources are bundled directly.
- Imported bindings and namespace members are snapshots at module-evaluation time (no live bindings) — see [Unimplemented: module system](../unimplemented/modules.md).
- Type-only imports and re-exports (`import type { T } from "..."`, `import { type T } from "..."`, `export type { T } from "..."`) are erased. They are **not** module edges: the target is not loaded or bundled, and a type-only cycle between modules is not a circular dependency. Use a side-effect import (`import "./mod"`) when a module must be evaluated without binding a name.

## Deferred evaluation

- The `gui` `.html` asset loader marks relative `<script src>` imports with a
  synthetic `__xt_deferred_module` binding. The bundler then wraps that module
  and its local import graph in functions that return their exports, and the
  generated script calls them after `loadHTML` (`__xt_defer` memoises each
  module). This matches a browser module script, where the graph is evaluated
  once the document exists.
- Modules also reachable through a normal (non-marker) import stay eager and get
  a thin wrapper over their already-initialised exports.
- The wrapper body is synchronous: top-level `await` inside a deferred script
  module is not supported.

## import.meta

- `import.meta.url` / `.dirname` / `.filename`, resolved from the running executable's path.

## Extension modules

- Extension modules (such as `fs`) are importable by bare or `node:`-prefixed specifier and resolve to their runtime entries; see [Extensions](./extensions.md).
