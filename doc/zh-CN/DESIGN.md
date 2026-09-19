# xbintsc 设计

> 语言 / Language：[English](../DESIGN.md) | **简体中文**

实现一个 TypeScript 的二进制编译器。

## 原始需求

1. 实现 JS 的各个基础类别（基础类型、function 等）的实现（以便后续 IR 的绑定）
2. 需要实现对 TypeScript 解析成 AST 树
3. 通过 LLVM 实现 IR 的绑定
4. 使用 llc 编译出 TypeScript 的二进制编译器
5. 再使用 TypeScript 的二进制编译器实现测试用例的 case

要求：

1. 需要实现增量编译
2. 合理的规划每个模块（解析与实现按不同目录存储）
3. 大量测试用例验证，测试用例也按模块划分
4. 支持扩展模块作为可选编译模块（如 nodejs 的 fs 模块、bun.js 的模块）
5. 后续能够实现自举（用编译器编译用 TS 重写的编译器）
6. 多系统支持（windows、mac、Ubuntu，以及不同芯片），用 GitHub Action 编译产物

## 实现架构

```
source.ts
  │  lexer    src/lexer      词法：完整 token 集合、模板、正则、ASI
  ▼
 tokens
  │  parser   src/parser     递归下降解析 → AST（src/ast）
  ▼
 AST
  │  binder   src/binder     作用域 / 符号 / 提升 / 闭包捕获分析
  ▼
 bound AST
  │  codegen  src/codegen    LLVM IR 文本（llvm.ts），values.ts 定义值模型
  ▼
 module.ll ──clang──► module.o ──link──► 可执行文件
                                    ▲
                          runtime/  C 运行时（NaN-boxing 值）
```

### 值模型

`xt_value` 是一个 64 位字。double 不做装箱直接存放；其它类型使用高 16 位 tag
加 48 位 payload 的带标签指针。表示法在 `src/codegen/values.ts` 与 `runtime/rt.h`
中定义一次，编译器与运行时共享。

### 调用约定

所有编译期函数使用统一 ABI：

```c
xt_value fn(xt_value env, int32_t argc, xt_value *argv);
```

`env` 通过 box 以引用方式传递捕获变量，直接调用与闭包调用共用一条代码路径。
不易内联的 JS 语义（`+` 隐式转换、关系比较、属性访问、打印）委托给 `@xt_*` 运行时调用。

### 运行时

`runtime/xt_runtime.c` 实现字符串、对象、数组、闭包、算术、比较、异常与
类 Node 的 `console.log` 打印。当前使用 bump arena，不释放内存——GC 被有意
推迟并隔离在 `xt_alloc` 之后，后续可在不改动编译器的前提下替换为精确/保守回收。

### llc 的替代

本机未安装 LLVM/`llc`，但 `clang` 可以直接编译 LLVM IR 文本，因此流程为
`IR 文本 → clang -c → 目标文件 → 链接 C 运行时`。这保持了 "IR 绑定 + 本地编译"
的目标，同时跨平台。

## 目录结构

| 目录 | 职责 |
| --- | --- |
| `src/lexer` | 词法分析（Scanner、TokenKind） |
| `src/ast` | AST 节点、工厂、访问器 |
| `src/parser` | 递归下降解析器（含类型语法） |
| `src/binder` | 作用域与符号解析、闭包捕获 |
| `src/codegen` | 值模型与 LLVM IR 生成 |
| `src/diagnostics` | 源文件、诊断、哈希 |
| `src/driver` | 流水线驱动、增量缓存、clang 工具链封装 |
| `src/extensions` | 可插拔扩展注册表与 Node 扩展 |
| `src/cli` | 命令行入口 |
| `runtime` | C 运行时与 `rt.h` |
| `tests` | 按模块划分的测试（含 e2e） |
| `scripts` | 运行时构建等脚本 |
| `.github/workflows` | 多系统多版本 CI |

## 增量编译

`src/driver/cache.ts` 以入口源码哈希、编译器版本、编译选项、平台与扩展集合
作为缓存键；当所有产物仍然存在时直接跳过构建。运行时的 C 源同样按内容哈希
缓存目标文件。

## 扩展机制

扩展是普通对象（见 `src/extensions/registry.ts`）：声明额外的 C 运行时代码、
链接参数，以及把全局函数映射到运行时符号（统一 `(argc, argv)` ABI）。Node 的
`readFileSync` 就是通过 `src/extensions/node/fs` + `runtime/ext_node/fs` 接入的，
核心编译器无需了解任何平台细节。

## 自举路线

编译器本身使用 TypeScript 编写并输出 IR 文本，`runtime` 已与编译器解耦。后续可
用 TS 重写运行时，并由 xbintsc 编译自身，逐步达成自举。

## 测试

按模块划分：`tests/lexer`、`tests/parser`、`tests/binder`、`tests/codegen`、
`tests/driver`、`tests/extensions`、`tests/cli`、`tests/e2e`。e2e 会在存在
`clang` 时真正编译并运行二进制，否则自动跳过。
