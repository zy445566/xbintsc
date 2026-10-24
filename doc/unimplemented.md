# xbintsc Unimplemented Syntax and Features

This documentation was compiled by checking the source (`src/`, `runtime/`) and
tests (`tests/`) file by file, and lists the syntax and features that are
**currently unimplemented / only partially implemented / semantically different
from the standard**.

Criteria (ordered by severity):

1. **Not parsed**: the parser reports a syntax error immediately; code generation is never reached.
2. **Parsed but codegen errors**: the AST / binding can be built, but `codegen` reports `UnsupportedFeature`.
3. **Parsed and compilable, but semantics are missing or differ**: a binary is produced, but the runtime result does not match ECMAScript / TypeScript semantics.

The pages are split by area so a limitation for one feature is recorded in one
small file. When a limitation is lifted, move the entry to the matching page
under [implemented](./implemented.md); git history records when it changed.

> Language: **English** | [简体中文](./zh-CN/unimplemented.md)

## Contents

- [Statements](./unimplemented/statements.md) — statement forms that do not lower
- [Expressions and operators](./unimplemented/expressions.md) — expression forms and operators that do not lower
- [Standard library](./unimplemented/stdlib.md) — what the built-in objects are still missing, including typed arrays
- [Module system](./unimplemented/modules.md) — circular dependencies, live bindings, CommonJS boundaries
- [Functions](./unimplemented/functions.md) — missing function semantics
- [Classes and OO](./unimplemented/classes.md) — access control, `abstract`/`implements`, private-name collisions
- [Type system](./unimplemented/types.md) — parsed and erased; only inference-free facts are checked
- [Runtime deviations](./unimplemented/deviations.md) — compiles and runs, but differs from ECMAScript
- [Toolchain and platform](./unimplemented/toolchain.md) — type checker, async I/O, GC modes, platform gaps
- [Node extension](./unimplemented/node.md) — Node module gaps, one page per area
