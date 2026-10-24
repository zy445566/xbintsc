# xbintsc 未实现语法与功能

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，列出**当前未实现 / 仅部分实现 / 语义与标准不符**的语法与功能。

判定标准（按严重程度）：

1. **未解析**：解析器直接报语法错误，无法进入代码生成。
2. **解析但代码生成报错**：AST / 绑定能建立，但 `codegen` 报 `UnsupportedFeature`。
3. **解析且能编译，但语义未实现或有偏差**：能产出二进制，但运行结果不符合 ECMAScript / TypeScript 语义。

文档按领域拆分为多个页面，某个功能的限制只记录在一个小文件里。限制解除后，把条目移到[已实现文档](../implemented.md)的对应页面；变更时间由 git 历史记录。

> 语言 / Language：[English](../unimplemented.md) | **简体中文**

## 目录

- [语句](./unimplemented/statements.md) —— 无法降级的语句形式
- [表达式与运算符](./unimplemented/expressions.md) —— 无法降级的表达式形式与运算符
- [标准库](./unimplemented/stdlib.md) —— 内建对象仍缺失的能力，含类型化数组
- [模块系统](./unimplemented/modules.md) —— 循环依赖、实时绑定、CommonJS 边界
- [函数](./unimplemented/functions.md) —— 缺失的函数语义
- [类与面向对象](./unimplemented/classes.md) —— 访问控制、`abstract`/`implements`、私有名冲突
- [类型系统](./unimplemented/types.md) —— 解析后擦除，只检查无需类型推断的事实
- [运行时偏差](./unimplemented/deviations.md) —— 能编译能运行，但与 ECMAScript 不同
- [工具链与平台](./unimplemented/toolchain.md) —— 类型检查器、异步 I/O、GC 模式、平台缺口
- [Node 扩展](./unimplemented/node.md) —— Node 模块缺口，按领域分页
