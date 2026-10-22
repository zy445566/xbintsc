# Module system

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/modules.md)

| Feature | Status |
| --- | --- |
| Multi-file / module resolution and linking | ✓ driver-layer AST bundling (`src/driver/modules.ts`): parse each module, rename top-level symbols by module prefix, rewrite references, merge into one file and rebind |
| Named import/export | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| Default import/export | ✓ `export default` / `import d from` |
| Re-export `export { x } from` / `export * from` | ✓ (`export *` copies the dependency's exports) |
| Namespace import `import * as ns` | ✓ lowered to a synthetic object literal holding every export |
| Circular dependencies | ✗ errors out (no circular initialization semantics) |
| Live bindings | ✗ namespace objects and imported bindings are snapshots at module-evaluation time |
| ESM `node_modules` packages | ✓ bare specifiers resolve up the tree through `exports` / `module` / `main`, including scoped packages and subpaths; the package sources are bundled like relative modules |
| CommonJS `require` / `module.exports` | ✓ inside `node_modules` only: the bundler gives each CommonJS package a synthetic `module` / `exports` pair, lowers `require("x")` to the bundled dependency's exports, and hoists external/built-in `require`s into ESM imports. `require()` in user code (outside `node_modules`) is still rejected with a diagnostic pointing at the ESM `import` form. |

> Extension modules (such as `fs`) are importable by bare or `node:`-prefixed
> specifier — `import { readFileSync } from "fs"` / `import path from "path"` —
> and resolve to their runtime entries (named, default and namespace forms).
> Relative modules and ESM packages under `node_modules` are bundled at the driver layer.

The implemented side of this contract is in [Module system](../implemented/modules.md).
