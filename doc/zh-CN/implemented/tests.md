# 测试

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/tests.md)

实现位置：`tests/`（`lexer` / `parser` / `binder` / `checker` / `codegen` / `driver` / `extensions` / `cli` / `conformance` / `e2e`）

- 各模块单元测试与源码树一一对应。
- `tests/e2e/` 会真正编译并运行二进制；缺少 `clang` 时自动跳过，而不是静默通过。
- `tests/e2e/differential-*.test.ts` 让同一份源码同时经 `xbintsc` 与 Node 运行并逐字节比较输出。语义变化时应新增差分用例，而不是手写期望字符串。
- `tests/conformance/` 是“支持子集”的可执行定义：它运行一份固定版本、随仓库分发的 [test262](https://github.com/tc39/test262) 子集（`tests/conformance/test262/cases/` 下的原始文件），逐个编译并执行受支持用例。不支持用例是 xbintsc 目前仍会拒绝的合法 JavaScript，以实际诊断码固定。[`doc/support-matrix.md`](../support-matrix.md) 由同一份目录生成（`npm run docs:matrix`），提交的表格过期时测试会失败。固定版本与 harness 适配见 [`tests/conformance/test262/SOURCE.md`](../../../tests/conformance/test262/SOURCE.md)。
- `tests/checker/` 固化检查器：未定义名、同作用域重复声明、实参个数、结构化可赋值性、属性形状与必须保持合法的收窄场景。
- 本目录的各功能页记录受支持的行为，测试是这份契约的可执行形式。提交前的门禁（`npm run typecheck && npm run lint && npm test`）见[参与贡献](../../ai/contributing.md)。
