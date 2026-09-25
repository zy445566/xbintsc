# xbintsc 已实现语法与功能

> 语言 / Language：[English](../implemented.md) | **简体中文**

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，仅列出**当前真正可用**的语法与功能。标注了对应实现位置，便于溯源。

> 说明：本编译器「解析」的范围远大于「生成代码」的范围。许多 TypeScript 语法可以被解析、甚至被绑定，但代码生成阶段会报 `UnsupportedFeature`。这些内容**不**列在这里，见 [未实现文档](unimplemented.md)。

---

## 1. 编译流水线（整体已实现）

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
- 流程由 `src/driver/compiler.ts` 串起：`读取 → 解析 → 绑定 → IR → 目标文件 → 链接`。
- **自举**：`xbintsc` 可编译自身前端。用编译器构建 `src/cli/main.ts` 可得到可用的 `xbintsc` 二进制，且该二进制再自编译时产出的 LLVM IR 逐字节一致（源码 ≡ 第 1 代 ≡ 第 2 代 ≡ 第 3 代）。运行时仍为手写 C。

---

## 2. 词法分析（Lexer，已实现）

实现位置：`src/lexer/scanner.ts`、`src/lexer/token.ts`

- 完整 Token 分类：标识符、关键字、私有标识符、数字、字符串、模板、正则（扫描）、所有标点与运算符。
- 完整关键字表：`abstract any as asserts async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object package private protected public readonly return satisfies set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield`
- 数字字面量：
  - 十进制、`0x` 十六进制、`0o` 八进制、`0b` 二进制
  - 下划线分隔 `1_000_000`
  - 小数、指数 `1.5e3`
  - BigInt 字面量（`10n`、`0xFFn`）求值为任意精度整数（算术、位运算、移位、比较、`toString(radix)`、`BigInt()` / `BigInt.asIntN` / `BigInt.asUintN`）
  - BigInt 字面量的值在词法 / 语法阶段即精确解析为 `bigint`（不经过 `Number`），超过 2^53 的字面量在 token / AST 中也保持完整精度
- 字符串字面量：
  - 单引号 / 双引号
  - 转义：`\n \t \r \b \f \0 \\ \' \"`、`\xHH`、`\uHHHH`、`\u{...}`
- 模板字面量（词法层）：无替换模板、模板头 `TemplateHead`、模板中 `TemplateMiddle`、模板尾 `TemplateTail`，支持 `${}` 与转义。
- 正则字面量扫描（`/pattern/flags`），并区分除法 `/`。
- 私有标识符 `#name`。
- 注释：单行 `//` 与块注释 `/* */`（含未闭合诊断）。
- 换行与空白跟踪（用于 ASI），BOM / CRLF 规范化（`SourceFile`）。
- 词法诊断：未终止字符串 / 模板 / 注释、非法字符、非法数字、非法转义。

---

## 3. 语法分析（Parser，已实现）

实现位置：`src/parser/parser.ts`、`src/ast/nodes.ts`

### 3.1 语句

- 变量声明：`var` / `let` / `const`，支持多声明符 `const a = 1, b = 2;`
- 函数声明、函数表达式和方法，含 `async` 修饰与生成器（`function*`、`yield`、`yield*`、`.next`/`.throw`/`.return`）
- `class` 声明 / 类表达式（构造函数、字段、方法、`static`、`extends`）
- `if` / `else`
- `while`、`do...while`
- `for`（初始化、条件、增量均可省略）
- `for...of`、`for...in`（见 3.5 语义限制）
- `return`、`break`、`continue`、`throw`（含带标签的 `break label` / `continue label`）
- `switch` / `case` / `default`（含穿透 fall-through）
- `try` / `catch` / `finally`（基于运行时 setjmp 帧的可捕获异常）
- `export var` / `export let` / `export const`（修饰符解析后擦除）
- 块语句 `{}`、空语句 `;`、`debugger;`
- 表达式语句

### 3.2 表达式

- 全部常见运算符及优先级 / 结合性（见第 5 节）
- 赋值表达式与全部复合赋值（见第 5 节）
- 条件（三元）表达式 `a ? b : c`
- 箭头函数 `() => expr` / `() => { ... }`（含类型参数、返回类型注解）
- 函数表达式 `function () {}` 与命名函数表达式 `function g() {}`
- 调用表达式 `f(...)`、成员访问 `a.b`、元素访问 `a[i]`
- 数组字面量 `[1, 2]`、稀疏数组 elision、展开 `[...a]`（也支持字符串、`Map`、`Set`）
- 对象字面量 `{ a: 1 }`、简写属性 `{ a }`、方法简写 `{ m() {} }`、getter/setter 简写 `{ get x() {} }` / `{ set x(v) {} }`、计算属性名 `{ [expr]: 1 }`、对象展开 `{ ...obj }`
- 模板字面量 `${}` 替换、标记模板（含 raw 字符串与 `String.raw`）
- 括号表达式、`as` / `satisfies` / 非空断言 `!`（类型擦除）
- 一元：`+ - ! ~ typeof void`、前缀 / 后缀 `++ --`
- 可选链 `?.` / `?.[]` / `?.()`（含空值短路语义）
- `delete` 表达式（删除对象属性）

### 3.3 TypeScript 类型语法（仅解析、结构保留后擦除）

- 类型注解、返回类型注解、类型参数 `<T>` 与约束 `<T extends U>`、类型参数默认值
- 类型引用、限定名 `A.B`
- 联合 `|`、交叉 `&`、数组 `T[]`、元组 `[T, U]`、可选元组成员 `T?`、剩余元组成员 `...T`
- 函数类型 `(a: T) => U`、构造签名 `new () => T`
- 对象类型字面量、属性签名、方法签名、索引签名 `[k: string]: T`
- 条件类型 `T extends U ? X : Y`、映射类型 `{ [K in T]: U }`、`infer`
- 类型运算符 `keyof`、`unique`、`readonly`
- `typeof`（类型查询）、索引访问类型 `T[K]`
- 字面量类型、`this` 类型
- 类型谓词 `x is T` / `asserts x is T`
- 接口、类型别名、枚举、命名空间 / 模块声明
- `import` / `export` 的各种形式（结构解析）

### 3.4 模块语法（结构解析 + 驱动打包）

- `import default, { named } from "..."`、`import * as ns from "..."`（相对模块的命名空间导入会降级为合成对象字面量；扩展模块如 `path` 亦已支持）、`import type`
- `export default`、`export { a as b }`、`export *`、`export =`
- import attributes（`with` / `assert`）
- `import` / `export` 的**运行时语义**由 `src/driver/modules.ts` 在驱动层完成：递归解析相对依赖**以及裸名称的 `node_modules` ESM 包**（遵循 `exports` / `module` / `main` 与包子路径），按模块前缀重命名顶层符号、改写引用，合并为单文件后重新绑定。包必须为 ESM；CommonJS `require()` 会报错并提示改用 `import`。循环依赖报错。

### 3.5 ASI

- 自动分号插入（Automatic Semicolon Insertion），依据 `precededByLineBreak` / `}` / EOF。

---

## 4. 名字绑定与作用域（Binder，已实现）

实现位置：`src/binder/binder.ts`

- 作用域种类：模块、函数、块、`for`、`catch`
- 符号种类：`var` / `let` / `const` / `function` / `parameter` / `class` / `interface` / `type` / `enum` / `import` / `namespace`
- `var` 与函数声明提升到函数作用域；`let` / `const` 保持块作用域
- 标识符 → 声明 的解析；未解析标识符收集（`CannotFindName`）
- 闭包捕获分析：被内层函数引用的外部变量标记 `captured` / `boxed`，并穿过中间闭包传递捕获索引
- 参数登记为局部符号；命名函数表达式自名登记为 `const`
- 类型专用声明（interface / type alias）不参与值捕获

---

## 5. 运算符与赋值（代码生成已实现）

实现位置：`src/codegen/llvm.ts`（`BINARY_RUNTIME`、`emitPrefix`、`emitPostfix`、`emitAssignment`）

### 5.1 算术

`+ - * / % **`（`**` 右结合）

### 5.2 比较与相等

`< <= > >=`、`== !=`（宽松相等）、`=== !==`（严格相等）

### 5.3 逻辑与短路

`&& || ??`（含短路求值）、`!`

### 5.4 位运算

`& | ^ ~ << >> >>>`

### 5.5 一元

`+ - ! ~`、前缀 / 后缀 `++ --`

### 5.6 赋值

`= += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??=`

### 5.7 其他表达式运算符

- 逗号表达式 `,`
- `in` 运算符（`key in obj`，映射 `xt_in`）
- `delete obj.key` / `delete obj[key]`（映射 `xt_delete`）
- `instanceof`（映射 `xt_instance_of`，沿原型链判断）

> `typeof` / `void` 作为一元运算符已实现。

---

## 6. 值模型与调用约定（已实现）

实现位置：`src/codegen/values.ts`、`runtime/rt.h`

- 所有 JS 值统一为 64 位 `xt_value`（NaN-boxing）。
- 双精度浮点不装箱；其它类型为高 16 位 tag + 48 位 payload 的带标签指针。
- Tag：`undefined` / `null` / `false` / `true` / `string` / `object` / `array` / `function`。
- 统一函数 ABI：

```c
xt_value fn(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
```

- `thisValue` 作为首个 ABI 参数线程化（异常安全、支持嵌套）；箭头函数从环境额外槽位词法继承 `this`。
- 闭包通过 `env` 线程化捕获变量（引用传递，box 包装），直接调用与闭包调用共用同一代码路径。
- `xt_object` 带原型字段，属性查找沿原型链；`xt_function` 带 `prototype` 与属性包（静态成员）。

---

## 7. LLVM IR 代码生成（Codegen，已实现）

实现位置：`src/codegen/llvm.ts`

- 生成 LLVM IR 文本（`.ll`），无需自建寄存器分配（依赖 `alloca` + mem2reg）。
- 语句 / 块边界值放在 `alloca`；条件与短路运算物化为临时槽，不使用 `phi`。
- 控制流：`if` / `while` / `do` / `for` / `for...of` / `for...in`，`switch`，`try/catch/finally`，`break` / `continue` / `return`。
  - `for...of` 与展开通过 `xt_iter_open` → `xt_iter_has` / `xt_iter_value` 迭代数组、字符串、`Map`、`Set`、生成器以及任何暴露 `[Symbol.iterator]()` 的对象（`Map` 产出 `[key, value]` 对）；非可迭代值抛出 `TypeError`。
  - `switch` 以严格相等逐 `case` 测试，命中后执行并在 `break` 前穿透。
  - `try/catch/finally` 通过运行时 `_setjmp` 帧实现：`xt_try_enter` 入栈、`_setjmp` 捕获、`xt_throw` 长跳转；IR 会把调用方的帧地址（`@llvm.frameaddress(0)`）作为 `_setjmp` 的第二个参数传入，与 clang 编译 MSVC 时的降级方式一致：Windows UCRT 的 `_setjmp` 会把这个帧存入 `_JUMP_BUFFER.Frame`，`longjmp` 再交给 `RtlUnwind` 执行栈展开；若不传该参数，`longjmp` 会展开到错误目标（`STATUS_BAD_FUNCTION_TABLE`）。使用 `_setjmp` 而非导出的 `setjmp` 符号，因为后者的 Windows ABI 是不兼容的双参数例程。含 `try` 的函数会强制局部变量驻留内存（内联汇编逃生点）以保证长跳转后值不丢失。
  - `for...in` 复用 `xt_object_keys` 枚举键（数组 / 字符串得到字符串下标）。
- 表达式：
  - 标识符、数字、BigInt（任意精度）、字符串、模板、布尔、`null`、`undefined`、`arguments`
  - 算术 / 比较 / 逻辑 / 短路 / 条件 / 位运算 / 一元（含 `typeof` `void`）/ 前后缀增减 / 复合赋值 / 逻辑赋值
  - 数组字面量（含展开 `[...]`）、对象字面量（含简写 / 方法 / 对象展开 `{...obj}`）
  - 属性访问（含 `length` 特判、`Math` 常量）、元素访问、调用
  - 可选链 `?.` / `?.[]` / `?.()`：以空值判断短路到 `undefined`
  - `delete`、`in`、`instanceof`
  - `this`（保存到函数 `%saved.this`；箭头函数从环境槽读）、`super`（`super.x` / `super(...)`）、`new`、`await`
  - 闭包（箭头函数 / 函数表达式）与捕获环境构建
- 类：构造器闭包 + 原型对象，存入 LLVM 全局（`@class.<id>`）；实例字段在构造器体前初始化；`static` 成员存于构造器属性包；`extends` 设置原型链；`super(...)` 通过原型上的隐藏 `__ctor` 调用。
- `async`：返回前用 `xt_promise_resolve` 包装；`await` 调用 `xt_await`（驱动微任务队列，遇 rejection 抬出异常）。
- 标准库调用：`console.*`、`Math.*`、`Object.*`、数组 / 字符串方法统一走 `xt_call_method` / `xt_math_call` / `xt_object_*`；`JSON`/`Date`/`Map`/`Set`/`RegExp`/`Promise` 静态与构造走对应 `xt_*`；全局函数（`parseInt` 等）走 `xt_parse_int` 等。
- 全局字符串池（`@.str.N` 私有常量，UTF-8 转义）。
- 内置调用：`console.log` / `info` / `warn` / `error`、`Math.*`、`Object.*`、数组 / 字符串方法、全局函数、扩展 builtins（统一 `(argc, argv)` ABI）。
- `main` 入口（返回 0，调用模块函数，并在返回前 `xt_drain_microtasks`）。
- 未支持节点统一报 `UnsupportedFeature`，不会崩溃。

---

## 8. C 运行时（Runtime，已实现）

实现位置：`runtime/xt_alloc.c`、`runtime/xt_values.c`、`runtime/xt_containers.c`、
`runtime/xt_stdlib.c`、`runtime/xt_stdlib2.c`、`runtime/xt_promise.c`、`runtime/xt_builtins.c`、
`runtime/xt_io.c`，共享私有头 `runtime/rt_internal.h`；公开 ABI 见 `runtime/rt.h`。

- 分配器：bump arena，`calloc` 分配，永不释放（GC 已隔离在 `xt_alloc` 之后）。
- 值构造：`xt_undefined/xt_null/xt_bool/xt_number/xt_string_new/xt_string_from_cstr`。
- 字符串：UTF-8 存储、拼接、相等比较、格式化数字转字符串。
- 类型转换：`xt_truthy`、`xt_to_number`、`xt_to_string`、`xt_typeof`。
- 算术：`add/sub/mul/div/mod/pow/neg/pos`。
- 位运算：`and/or/xor/not/shl/shr/ushr`（含 `ToInt32` 语义）。
- 比较：`lt/le/gt/ge`、宽松 / 严格相等、`not`、`is_nullish`。
- 对象：线性属性列表，`object_new/get/set/has/keys/values/entries/assign/spread`。
- 数组：`array_new/get/set/push/length/spread`；对 `arr.length` 赋值会截断 / 扩展（与 JS 一致）；`iter_length` / `iter_value` 为数组、字符串、`Map`、`Set` 提供统一迭代视图。
- Symbol（`xt_symbol.c`）：`Symbol(description)` 原始值（`XT_OBJECT_KIND_SYMBOL`）、13 个著名符号（`Symbol.iterator`、`Symbol.asyncIterator`、`Symbol.match` 等）、`Symbol.for` / `Symbol.keyFor` 全局注册表、`symbol.description` / `toString()` / `valueOf()`；symbol 可作为属性键（`Object.getOwnPropertySymbols`，symbol 键的 `get`/`set`/`in`/`delete`），会被 `Object.keys` / `values` / `entries` / `for...in` / `JSON.stringify` 跳过，打印为 `Symbol(desc)`。
- 通用成员访问：`xt_get` / `xt_set`（对数组 / 对象 / 字符串分发）。
- 标准库：`xt_call_method`（统一分发数组 / 字符串方法与对象上的函数属性）、`xt_math_call`（`Math.*` 与常量）、全局函数 `xt_parse_int/parse_float/is_nan/is_finite/number_ctor/string_ctor/boolean_ctor`。
- 运算符辅助：`xt_in`（`in`）、`xt_delete`（`delete`）、`xt_rest_args`（剩余参数 / `arguments`）。
- Box：`box_new/get/set`（用于闭包捕获变量）。
- 函数与闭包：`arg`、`closure_new/call/env/arity`。
- 异常：`xt_try_enter` / `xt_try_exception` / `xt_try_leave` 维护 `_setjmp` 帧栈；`xt_throw` 在存在帧时长跳到最近 `try`，否则打印 `Uncaught ...` 后退出。
- 输出：`xt_print/xt_println/xt_console_log/info/warn/error`（Node 风格 inspect：数组 `[ a, b ]`、对象 `{ k: v }`；`info`/`log` 到 stdout，`warn`/`error` 到 stderr），以及 `dir/trace/assert/count/countReset/group/groupEnd/table/time/timeEnd/timeLog`。
- 对象 / 函数：`xt_object` 带原型链，`xt_function` 带属性包（静态成员与 `prototype`）；`xt_new`（实例化）、`xt_instance_of`（原型链）、`xt_object_freeze/is_frozen/from_entries`。
- 标准库扩展（`xt_stdlib2.c`）：数组 / 字符串 / 数字 / 对象的扩展方法；`Object` / `Array` / `Number` / `String` 静态方法；`JSON.parse` / `JSON.stringify`；`Map` / `Set`；`Date`（`gmtime_r`）；`RegExp`（POSIX ERE `regcomp`/`regexec` 的 `test`/`exec`）。
- Promise（`xt_promise.c`）：同步微任务队列（`xt_microtasks`）；`xt_promise_ctor/resolve/reject/static`、实例 `then/catch/finally`；`xt_await` 驱动队列直到 settle，rejection 触发 `xt_throw`；程序结束时 `xt_drain_microtasks` 清空队列。

---

## 9. 扩展机制（Extensions，已实现）

实现位置：`src/extensions/registry.ts`、`src/extensions/node/`

- 扩展是普通对象：`runtimeSources()`（额外 C 源）、`linkerFlags()`（额外链接参数）、`builtins()`（全局标识符 → 运行时符号，统一 `(argc, argv)` ABI）与 `modules()`（导入说明符 → 命名导出 / 命名空间）。
- `ExtensionRegistry`：注册 / 注销 / 查询 / 汇总 builtins、modules、runtime 源、链接参数。
- 内置核心扩展 `core`：暴露 `print`（映射 `xt_println`），始终注册。
- Node 扩展 `node`：
  - 模块化组织：`src/extensions/node/fs/` + `runtime/ext_node/fs/read_file.c`
  - 暴露可导入模块（`fs`、`fs/promises`、`path`、`os`、`process` 等），同时支持裸名称与 `node:` 前缀；`import { readFileSync } from "fs"` 解析到 `xt_node_read_text_file`，`path`/`os`/`process` 的导出映射到命名空间分发器。
- 添加新模块只需新增目录 + C 实现，核心编译器无需改动。
- 原生 C++/Rust 扩展（`src/extensions/native.ts`、`--ext-native`）：
  - `nativeObjects()` 链接以 `(argc, argv)` ABI 暴露 `extern "C"` 符号的预编译对象/静态库；JSON manifest 把它们映射为 builtins/modules（`linkerFlags` / `linkerFlagsByPlatform` 用于 C++/Rust 运行时）。
  - 编写辅助位于 `runtime/xt_ext.h`（C/C++）与 `runtime/xt_ext.rs`（Rust）；可运行工程在 `examples/extensions/`。
  - 驱动原样链接这些产物，增量缓存会对其内容取指纹，因此重建库会使缓存二进制失效。
  - CI 在 Linux、macOS、Windows 上都会构建两种语言的示例；Windows 使用自带的 MinGW-w64 工具链（C++ `-lc++ -static`，Rust `*-pc-windows-gnullvm` + `-lntdll -static`）。

---

## 10. 驱动、增量编译与工具链（已实现）

实现位置：`src/driver/compiler.ts`、`src/driver/cache.ts`、`src/driver/toolchain.ts`、`src/driver/paths.ts`

- 编译流水线：读源 → 模块打包（`src/driver/modules.ts`，当入口含 `import`/`export` 时）→ 解析 → 绑定/检查 → IR → 目标文件 → 链接。
- 增量缓存：以「编译器版本 + 源码哈希 + emit 类型 + 优化级别 + 平台 + 扩展指纹（名称、链接参数、原生对象内容）」为键，产物存在且新鲜则跳过构建。
- C 运行时与扩展源按内容哈希缓存目标文件，只编译一次。
- 工具链封装：查找 `clang`（可用 `xbintsc_CLANG` 覆盖）、编译 IR、编译 C、链接。
- 链接参数：非 Windows 自动加 `-lm`；扩展可追加链接参数。

---

## 11. CLI 与编程接口（已实现）

实现位置：`src/cli/main.ts`、`src/index.ts`、`bin/xbintsc.js`

### 11.1 CLI 命令

```
xbintsc build <file.ts> [options]   编译为原生二进制
xbintsc run   <file.ts> [-- args]   编译并执行
xbintsc emit  <file.ts>             打印 LLVM IR
xbintsc version                     打印版本
xbintsc help                        帮助
```

### 11.2 CLI 选项

```
-o, --output <path>   输出路径
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0..-O3              优化级别（默认 -O2）
    --ext <names>     逗号分隔扩展（如 node）
    --ext-native <m>  从 JSON manifest 注册 C++/Rust 扩展
    --force           忽略增量缓存
    --verbose         打印进度信息
```

### 11.3 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

---

## 12. 测试（已实现）

实现位置：`tests/`（`lexer` / `parser` / `binder` / `codegen` / `driver` / `extensions` / `cli` / `e2e`）

- 各模块单元测试；e2e 在存在 `clang` 时真正编译并运行二进制，否则自动跳过。
- e2e 覆盖：算术与打印、递归函数、循环 / 数组 / 字符串拼接、闭包按引用捕获、对象 / 数组 JS 风格打印、Node `fs` 扩展（经 `import`）、`switch` 穿透、数组 / 字符串方法、`Math` 与全局函数与 `console` 各等级、默认 / 剩余参数与 `arguments`、`Object` 助手与展开与 `in`/`delete`、`for...in` 对象键枚举、`try/catch/finally`、可选链、类与 `new`/`this`/`static`/`extends`/`super`/`instanceof`、`async`/`await` 与 `Promise`、`Map`/`Set`/`JSON` 与扩展标准库、`Map`/`Set` 的 `for...of`、数组 `length` 赋值与可迭代展开、多文件 `import`/`export`。

---

## 13. 已实现特性速查表

| 类别 | 内容 |
| --- | --- |
| 声明 | `var` `let` `const`、函数声明、函数表达式、箭头函数、`class`（声明 / 表达式）、接口 / 类型别名（擦除） |
| 控制流 | `if/else`、`while`、`do...while`、`for`、`for...of`、`for...in`、`switch`、`try/catch/finally`、`break`、`continue`、`return`、`throw` |
| 表达式 | 标识符、字面量、模板字符串、数组 / 对象字面量（含展开）、调用、成员 / 元素访问、可选链、闭包、`arguments`、`this`、`new`、`super`、`await` |
| 运算符 | 算术、比较、相等、逻辑、位运算、移位、一元（含 `typeof`/`void`）、前后缀增减、复合赋值、逻辑赋值、`in`、`delete`、`instanceof` |
| 函数 | 默认参数、剩余参数、捕获闭包、`this` 绑定、箭头函数词法 `this`、`call`/`apply`/`bind`、`name`/`length` |
| 类 / OO | 构造函数、实例字段、方法、`static`、继承 `extends`/`super`、原型链、`instanceof` |
| 异步 | `async`/`await`、`Promise`（`then/catch/finally`、`resolve/reject/all/allSettled/race`）、同步微任务队列 |
| 模块 | `import`/`export`（具名 / 默认 / 再导出 / `export *`），相对路径多文件打包（`.js` 说明符解析到 `.ts` 源码）与 ESM `node_modules` 包（`exports` / `module` / `main`、作用域包与子路径），裸说明符解析到扩展模块；CommonJS `require()` 报错并提示改用 `import` |
| 标准库 | 数组 / 字符串 / 数字 / 对象扩展方法、`Math`、`JSON`、`Date`、`RegExp`、`Map`、`Set`、`Symbol`、`Error` 家族、`Object/Array/Number/String/Symbol` 静态、`console.*` |
| 值模型 | 64 位 NaN-boxing、统一函数 ABI（含 `this`）、闭包环境、对象原型链 |
| 运行时 | 字符串 / 对象 / 数组 / 闭包 / 算术 / 比较 / 可捕获异常 / Promise / 集合 / symbol / 生成器 / `console` |
| 扩展 | 扩展注册表、`core`（print）、`node`（fs / path / os / process / buffer / stream / net / dgram / http，按说明符导入） |
| 工具链 | clang 编译 IR/C、链接、增量缓存 |
| 自举 | `xbintsc` 可将 `src/cli/main.ts` 编译为原生二进制；产出的 IR 从第 1 代起达到不动点 |
| 平台 | macOS / Linux / Windows（构建层面已适配，CI 见 `.github/workflows`） |
