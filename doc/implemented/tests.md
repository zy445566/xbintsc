# Tests

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/tests.md)

Location: `tests/` (`lexer` / `parser` / `binder` / `codegen` / `driver` / `extensions` / `cli` / `e2e`)

- Per-module unit tests mirror the source tree.
- `tests/e2e/` compiles real programs and runs the resulting binaries. When `clang` is absent the suite skips itself automatically; it is not a silent pass.
- `tests/e2e/differential-*.test.ts` runs the same source through both `xbintsc` and Node and compares output byte-for-byte. When semantics change, add a differential case instead of a hand-written expected string.
- Feature pages in this directory record the supported behaviour; the tests are the executable form of that contract. See [Contributing](../ai/contributing.md) for the gate (`npm run typecheck && npm run lint && npm test`).
