# 编译流水线

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/pipeline.md)

```
source.ts
   │  词法分析  src/lexer/scanner.ts + token.ts
   ▼
 tokens
   │  语法分析  src/parser/parser.ts  →  AST  src/ast/
   ▼
 AST
   │  名字绑定  src/binder/binder.ts  →  作用域/符号/闭包捕获
   ▼
 bound AST
   │  代码生成  src/codegen/llvm.ts + values.ts  →  LLVM IR 文本
   ▼
 module.ll ── clang ──► module.o ──链接──► 可执行文件
                                   ▲
                          runtime/*.c（C 运行时，按功能拆分）
```

- 前端与后端、运行时、扩展完全解耦。
- 流程由 `src/driver/compiler.ts` 串起：`读取 → 解析 → 绑定 → 检查 → IR → 目标文件 → 链接`。前端位于 `src/driver/frontend.ts`，缓存键位于 `src/driver/build-key.ts`，运行时编译位于 `src/driver/runtime-objects.ts`，平台产物位于 `src/driver/app-artifacts.ts`。
- [语义检查](./checker.md)（`src/checker/`）在绑定与代码生成之间运行，`--no-check` 可跳过。它把无需推断的检查（未定义名、重复声明、实参个数）与一个小型结构化类型模型结合起来（确定的可赋值性、属性形状、简单泛型与流收窄）；类型语法随后仍然被擦除。
- **自举**：`xbintsc` 可编译自身前端。用编译器构建 `src/cli/main.ts` 可得到可用的 `xbintsc` 二进制，且该二进制再自编译时产出的 LLVM IR 逐字节一致（源码 ≡ 第 1 代 ≡ 第 2 代 ≡ 第 3 代）。运行时仍为手写 C。
- **规范产物**：所有逐字节比较的产物在所有宿主上使用同一种编码。`src/canonical.ts` 定义 IR/调试信息/指纹的路径分隔符（`/`）与换行（LF），原生路径进入产物时用 `artifactPath()` 转换；运行时在渲染 `path` 结果与 `file://` URL 时使用 `XT_ARTIFACT_PATH_SEPARATOR`（`runtime/rt.h`）。面向用户的原生路径——诊断、`import.meta.filename`、`process.cwd()`——保留宿主分隔符，与 Node 一致。
