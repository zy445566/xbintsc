# xbintsc 已实现语法与功能

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，仅列出**当前真正可用**的语法与功能。标注了对应实现位置，便于溯源。

文档按编译阶段与功能域拆分为多个页面，改动只落在对应的小文件里，避免所有分支都修改同一份大文档。

> 说明：本编译器「解析」的范围远大于「生成代码」的范围。许多 TypeScript 语法可以被解析、甚至被绑定，但代码生成阶段会报 `UnsupportedFeature`。这些内容**不**列在这里，见[未实现文档](../unimplemented.md)。

> 语言 / Language：[English](../implemented.md) | **简体中文**

## 目录

### 流水线与前端

- [编译流水线](./implemented/pipeline.md) —— 源码 → token → AST → IR → 二进制，自举
- [词法分析](./implemented/lexer.md) —— token、字面量、关键字、词法诊断
- [语法分析](./implemented/parser.md) —— 语句、表达式、类型语法、ASI
- [名字绑定与作用域](./implemented/binder.md) —— 作用域、符号、闭包捕获

### 语义与代码生成

- [运算符与赋值](./implemented/operators.md) —— 算术、比较、逻辑、位运算、赋值
- [值模型与调用约定](./implemented/value-model.md) —— NaN-boxing、函数 ABI、闭包、原型
- [LLVM IR 代码生成](./implemented/codegen.md) —— 控制流、表达式、类、内联缓存、入口

### 运行时与标准库

- [C 运行时核心](./implemented/runtime-core.md) —— 分配器/GC、值、转换、对象、异常
- [字符串与文本](./implemented/strings.md) —— UTF-8/UTF-16 模型、字符串方法、文本编解码
- [标准库](./implemented/stdlib.md) —— Object、Array、Number/BigInt/Math、JSON、Date、RegExp、Symbol、错误、console
- [集合](./implemented/collections.md) —— Map、Set、WeakMap、WeakSet、WeakRef、迭代
- [类型化数组与二进制数据](./implemented/typed-arrays.md) —— 类型化数组、ArrayBuffer、DataView
- [异步、Promise 与生成器](./implemented/async.md) —— async/await、异步生成器、Promise、定时器
- [fetch](./implemented/fetch.md) —— 阻塞式 HTTP/1.1 客户端与 Headers

### 模块与工具

- [模块系统](./implemented/modules.md) —— import/export、打包、`node_modules`、`import.meta`
- [扩展机制](./implemented/extensions.md) —— 注册表、`node` 扩展、原生 C++/Rust 扩展
- [Node 扩展](./implemented/node.md) —— 21 个 Node 内置模块，每个模块一页
- [驱动、增量编译与工具链](./implemented/driver.md) —— 缓存、clang/LTO/PGO、配置、图标
- [CLI 与编程接口](./implemented/cli.md) —— 命令、参数、嵌入式 API
- [测试](./implemented/tests.md) —— 单元测试、e2e 与差分测试

## 改动应该写在哪一页？

按功能选择归属页面，可以让无关分支不修改同一个文件。

| 你改动了…… | 编辑 |
| --- | --- |
| 分词器、字面量、关键字 | [词法分析](./implemented/lexer.md) |
| 语句 / 表达式 / 类型语法 | [语法分析](./implemented/parser.md) |
| 作用域、符号、捕获 | [名字绑定](./implemented/binder.md) |
| 运算符、赋值降级 | [运算符与赋值](./implemented/operators.md) |
| 值表示、函数 ABI | [值模型](./implemented/value-model.md) |
| 控制流、表达式降级、类、`main` | [代码生成](./implemented/codegen.md) |
| 分配器、GC、类型转换、异常 | [运行时核心](./implemented/runtime-core.md) |
| 字符串模型与方法 | [字符串与文本](./implemented/strings.md) |
| Object/Array/Number/Math/JSON/Date/RegExp/Symbol/错误/console | [标准库](./implemented/stdlib.md) |
| Map/Set/WeakMap/WeakSet/WeakRef/迭代 | [集合](./implemented/collections.md) |
| 类型化数组、ArrayBuffer、DataView | [类型化数组](./implemented/typed-arrays.md) |
| async/await、Promise、生成器、定时器 | [异步](./implemented/async.md) |
| HTTP 客户端 | [fetch](./implemented/fetch.md) |
| import/export 与打包 | [模块系统](./implemented/modules.md) |
| 扩展注册表或 `node` 扩展 | [扩展机制](./implemented/extensions.md) |
| 某个 Node 模块（`fs`、`path`、`http` 等） | [Node 扩展](./implemented/node.md) |
| 构建缓存、clang、LTO/PGO、配置、图标 | [驱动](./implemented/driver.md) |
| CLI 参数或嵌入式 API | [CLI](./implemented/cli.md) |
| 测试与差分测试 | [测试](./implemented/tests.md) |
