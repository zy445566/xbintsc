# 测试

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/tests.md)

实现位置：`tests/`（`lexer` / `parser` / `binder` / `codegen` / `driver` / `extensions` / `cli` / `e2e`）

- 各模块单元测试与源码树一一对应。
- `tests/e2e/` 会真正编译并运行二进制；缺少 `clang` 时自动跳过，而不是静默通过。
- `tests/e2e/differential-*.test.ts` 让同一份源码同时经 `xbintsc` 与 Node 运行并逐字节比较输出。语义变化时应新增差分用例，而不是手写期望字符串。
- 本目录的各功能页记录受支持的行为，测试是这份契约的可执行形式。提交前的门禁（`npm run typecheck && npm run lint && npm test`）见[参与贡献](../../ai/contributing.md)。
