# Tests

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/tests.md)

Location: `tests/` (`lexer` / `parser` / `binder` / `checker` / `codegen` / `driver` / `extensions` / `cli` / `conformance` / `e2e`)

- Per-module unit tests mirror the source tree.
- `tests/e2e/` compiles real programs and runs the resulting binaries. When `clang` is absent the suite skips itself automatically; it is not a silent pass.
- `tests/e2e/differential-*.test.ts` runs the same source through both `xbintsc` and Node and compares output byte-for-byte. When semantics change, add a differential case instead of a hand-written expected string.
- `tests/conformance/` is the executable definition of the supported subset: it runs a pinned, vendored subset of [test262](https://github.com/tc39/test262) (unmodified files under `tests/conformance/test262/cases/`), compiling and executing each supported case. Unsupported cases are valid JavaScript that xbintsc still rejects, pinned with the diagnostic code it produces. [`doc/support-matrix.md`](../support-matrix.md) is generated from the same catalogue (`npm run docs:matrix`), and a test fails when the committed table is stale. See [`tests/conformance/test262/SOURCE.md`](../../tests/conformance/test262/SOURCE.md) for the pinned commit and the harness adaptation.
- `tests/checker/` pins the checker: unresolved names, same-scope redeclarations, call arity, structural assignability, property shapes and the narrowings that must stay legal.
- Feature pages in this directory record the supported behaviour; the tests are the executable form of that contract. See [Contributing](../ai/contributing.md) for the gate (`npm run typecheck && npm run lint && npm test`).
