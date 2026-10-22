# LLVM IR 代码生成（Codegen）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/codegen.md)

实现位置：`src/codegen/llvm.ts`

- 生成 LLVM IR 文本（`.ll`），无需自建寄存器分配（依赖 `alloca` + mem2reg）。
- 语句 / 块边界值放在 `alloca`；条件与短路运算物化为临时槽，不使用 `phi`。
- 属性读取按源码位置生成内联缓存：每个访问点拥有一个零初始化的 `[2 x i64]` 全局（`@ic.N`），传给 `xt_get_ic`。普通对象带有驻留的隐藏类 shape（`xt_shape`，`(parent, key)` 转移树），缓存保存 `(shape, 属性下标)`，命中时只需比较 shape 指针并校验一次键，无需线性扫描属性表。插入/删除属性以及整数键重排会重建或转移 shape，从而使旧缓存失效；数组、函数与容器回退到 `xt_get`。
- 数值运算符保留内联双精度快路径；数值字面量操作数跳过标签检查。若条件表达式本身是比较、`in`、`instanceof` 或 `!`，其值已经是布尔标签，代码生成用 `icmp eq i64 value, XT_TRUE` 代替调用 `xt_truthy`（热循环条件因此少一次调用）。
- 控制流：`if` / `while` / `do` / `for` / `for...of` / `for...in`，`switch`，`try/catch/finally`，`break` / `continue` / `return`。
  - `for...of` 与展开通过 `xt_iter_open` → `xt_iter_has` / `xt_iter_value` 迭代数组、字符串、类型化数组、`Map`、`Set`、生成器以及任何暴露 `[Symbol.iterator]()` 的对象（`Map` 产出 `[key, value]` 对）；非可迭代值抛出 `TypeError`。数组还暴露稳定的 `Array.prototype[Symbol.iterator]`，调用后返回带 `next()` 且自身可迭代的迭代器对象。
  - `switch` 以严格相等逐 `case` 测试，命中后执行并在 `break` 前穿透。
  - `try/catch/finally` 通过运行时 `_setjmp` 帧实现：`xt_try_enter` 入栈、`_setjmp` 捕获、`xt_throw` 长跳转；IR 会把调用方的帧地址（`@llvm.frameaddress(0)`）作为 `_setjmp` 的第二个参数传入，与 clang 编译 MSVC 时的降级方式一致：Windows UCRT 的 `_setjmp` 会把这个帧存入 `_JUMP_BUFFER.Frame`，`longjmp` 再交给 `RtlUnwind` 执行栈展开；若不传该参数，`longjmp` 会展开到错误目标（`STATUS_BAD_FUNCTION_TABLE`）。使用 `_setjmp` 而非导出的 `setjmp` 符号，因为后者的 Windows ABI 是不兼容的双参数例程。含 `try` 的函数会强制局部变量驻留内存（内联汇编逃生点）以保证长跳转后值不丢失。
  - `for...in` 复用 `xt_object_keys` 枚举键（数组 / 字符串得到字符串下标）。
- 表达式：
  - 标识符、数字、BigInt（任意精度）、字符串、模板、布尔、`null`、`undefined`、`arguments`
  - 算术 / 比较 / 逻辑 / 短路 / 条件 / 位运算 / 一元（含 `typeof` `void`）/ 前后缀增减 / 复合赋值 / 逻辑赋值
  - 数组字面量（含展开 `[...]`）、对象字面量（含简写 / 方法 / 对象展开 `{...obj}`）
  - 类型化数组、`ArrayBuffer`、`DataView`、`TextEncoder` / `TextDecoder`：见[类型化数组与二进制数据](./typed-arrays.md)与[字符串与文本](./strings.md)
  - 属性访问（含 `length` 特判、`Math` 常量）、元素访问、调用
  - 一等公民内建构造器（`const A = Array`、`const M = Map`、`const E = TypeError`、类型化数组）：裸引用下降为 `xt_builtin_constructor`，得到带 `name` / `length`、`prototype` 与静态成员（`A.isArray`、`A.from`、`M.prototype` 等）的缓存函数。通过别名调用或 `new` 会分派到同一运行时构造器，`x instanceof A` 沿共享原型链判断。
  - 可选链 `?.` / `?.[]` / `?.()`：以空值判断短路到 `undefined`
  - `delete`、`in`、`instanceof`
  - `this`（保存到函数 `%saved.this`；箭头函数从环境槽读）、`super`（`super.x` / `super(...)`）、`new`、`await`
  - 闭包（箭头函数 / 函数表达式）与捕获环境构建
- 类：构造器闭包 + 原型对象，存入 LLVM 全局（`@class.<id>`）；实例字段在构造器体前初始化；`static` 成员存于构造器属性包；`extends` 设置原型链；`super(...)` 通过原型上的隐藏 `__ctor` 调用。
- `async` / `async function*`：闭包分别用 `xt_function_set_async` / `xt_function_set_async_generator` 标记，并在栈式协程上运行——见[异步、Promise 与生成器](./async.md)。
- 标准库调用统一走 `xt_call_method` / `xt_math_call` / `xt_object_*` 与各构造器的 `xt_*` 入口；完整表面见[标准库](./stdlib.md)。
- 全局字符串池（`@.str.N` 私有常量，UTF-8 转义）。
- 内置调用：`console.log` / `info` / `warn` / `error`、`Math.*`、`Object.*`、数组 / 字符串方法、全局函数、扩展 builtins（统一 `(argc, argv)` ABI）。
- `main` 入口（返回 0，调用模块函数，并在返回前 `xt_drain_microtasks`）。
- 未支持节点统一报 `UnsupportedFeature`，不会崩溃。
