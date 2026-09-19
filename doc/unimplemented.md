# xbintsc 未实现语法与功能

本文档基于对源码（`src/`、`runtime/`）与测试（`tests/`）的逐文件核对整理，列出**当前未实现 / 仅部分实现 / 语义与标准不符**的语法与功能。

判定标准（按严重程度）：

1. **未解析**：解析器直接报语法错误，无法进入代码生成。
2. **解析但代码生成报错**：AST / 绑定能建立，但 `codegen` 报 `UnsupportedFeature`。
3. **解析且能编译，但语义未实现或有偏差**：能产出二进制，但运行结果不符合 ECMAScript / TypeScript 语义。

---

## 1. 语句层未实现

### 1.1 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `class` 声明 / 类表达式 | 解析 ✓，代码生成 ✗ | `class Foo {}` → "does not yet support this statement (class declaration)" |
| `new` 表达式 | 解析 ✓，代码生成 ✗ | `new Foo()` → "does not yet support this expression (new expression)" |
| `enum` 声明 | 解析 ✓，代码生成 ✗ | `enum E { A, B }` → "does not yet support this statement (enum declaration)" |
| `switch` / `case` / `default` | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (switch statement)" |
| `try` / `catch` / `finally` | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (try statement)" |
| `namespace` / `module` 声明 | 解析 ✓，代码生成 ✗ | → "does not yet support this statement (module declaration)" |
| `import` 声明 | 解析 ✓，代码生成 ✗ | `import { x } from "fs"` → "does not yet support this statement (import declaration)" |

### 1.2 解析器不支持（直接语法错误）

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| 标签语句 `label: statement` | 未解析 | `parseStatement` 无对应分支；`label:` 会报 "Unexpected token ':'" |
| `export var/let/const` 变量导出 | 未解析 | `export const x = 1` 报 "Unexpected token 'const'"（仅 `export function/class/interface/type/enum/namespace/default/=/ * / {}` 被解析） |

> 注：`LabeledStatement`、`ExportDeclaration` 等 AST 节点虽已定义，但解析路径缺失或后端不处理。

---

## 2. 表达式层未实现

### 2.1 解析但代码生成报 `UnsupportedFeature`

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| `this` 表达式 | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (this expression)" |
| 标记模板 `` f`...` `` | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (tagged template)" |
| `await` 表达式 | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (await expression)" |
| `yield` 表达式（生成器） | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (yield expression)" |
| 对象展开 `{...obj}` | 解析 ✓，代码生成 ✗ | → "does not yet support this object spread (spread element)" |
| `delete` 表达式 | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (delete expression)" |
| 值级 `typeof x` | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (40)"（`TypeOfExpression` 未被 `emitExpression` 处理） |
| 值级 `void x` | 解析 ✓，代码生成 ✗ | → "does not yet support this expression (41)"（`VoidExpression` 未被处理） |
| `new` 表达式 | 解析 ✓，代码生成 ✗ | 同 1.1 |

### 2.2 解析器不支持

| 语法 | 现状 | 说明 |
| --- | --- | --- |
| 正则表达式字面量作为表达式 | 未解析 | scanner 能扫描 `/re/`，但 `parsePrimaryExpression` 无 `RegularExpressionLiteral` 分支，报 "Unexpected token '/…/'" |

### 2.3 运算符未实现（代码生成报错）

| 运算符 | 现状 | 说明 |
| --- | --- | --- |
| `in` | 解析 ✓，代码生成 ✗ | `BINARY_RUNTIME` 无 `In` 映射 → "does not yet support this operator 'in'" |
| `instanceof` | 解析 ✓，代码生成 ✗ | `BINARY_RUNTIME` 无 `InstanceOf` 映射 → "does not yet support this operator 'instanceof'"（且 `Object` 等构造器名未定义） |

---

## 3. 数组 / 标准库方法未实现

### 3.1 数组方法

`tryEmitBuiltinCall` 中识别了这些方法名，但**仅 `push` 有实现**，其余均报 unsupported：

| 方法 | 现状 |
| --- | --- |
| `push` | ✅ 已实现 |
| `pop` `shift` `unshift` | ✗ "does not yet support this array method '…'" |
| `join` `slice` `indexOf` `includes` | ✗ 同上 |
| `map` `forEach` `filter` `reduce` | ✗ 同上 |

### 3.2 其它标准库

| 类别 | 现状 |
| --- | --- |
| `Math.*` | ✗ 未实现（无任何 `Math` 绑定） |
| `String` 方法（`charAt` `split` `replace` `toUpperCase` 等） | ✗ 未实现（仅 `.length` 与数字下标访问可用） |
| `Object` 方法（`keys` `values` `entries` `assign` 等） | ✗ 未实现（`xt_object_keys` 运行时存在，但无 builtin 暴露） |
| `JSON.parse` / `JSON.stringify` | ✗ 未实现 |
| `Date`、`RegExp`、`Map`、`Set`、`Promise` | ✗ 未实现 |
| 全局函数 `parseInt` `parseFloat` `isNaN` `Number` `String` `Boolean` `Array` `Object` 等 | ✗ 未实现 |
| `console` 除 `log` 外（`error` `warn` `info` `dir` 等） | ✗ 未实现（仅 `console.log`） |
| `arguments` 对象 | ✗ 未实现 |

---

## 4. 模块系统未实现

| 功能 | 现状 |
| --- | --- |
| 多文件 / 模块解析与链接 | ✗ 未实现（编译以单文件为入口，`import` 报 unsupported） |
| 具名 / 默认导入导出（运行时） | ✗ 未实现（仅结构解析；`export var/let/const` 甚至无法解析） |
| 命名空间导出 | ✗ 未实现 |
| 第三方 / npm 依赖 | ✗ 未实现 |

> 扩展 builtins（如 `readFileSync`）是「全局符号 → C 符号」的静态映射，不是真正的模块导入。

---

## 5. 函数特性未实现（或语义缺失）

| 特性 | 解析 | 语义 |
| --- | --- | --- |
| 默认参数 `function f(a = 5)` | ✓ | ✗ 忽略默认值；缺参时得到 `undefined` 而非默认值 |
| 剩余参数 `function f(...args)` | ✓ | ✗ 不构造 `args` 数组；`args` 被当作单个普通参数 |
| `this` 绑定 / 方法调用语义 | ✓（`this` 解析） | ✗ `this` 代码生成报错 |
| `new.target` | ✗ | 未实现 |
| 生成器 / 迭代器 | ✗（`yield` 代码生成报错） | 未实现 |
| `async` / `await` / Promise | ✗（`await` 代码生成报错） | 未实现 |
| 闭包 `arity` / 函数属性 | — | `xt_closure_arity` 恒为 -1，未填充 |

---

## 6. 类与面向对象未实现

| 特性 | 现状 |
| --- | --- |
| `class` 声明 / 类表达式 | ✗ 代码生成报错 |
| 构造函数 `constructor` | ✗ |
| 字段 / 属性声明（含参数属性） | ✗ |
| 方法、`get` / `set` 访问器 | ✗ |
| `static`、`private` / `protected` / `public` 修饰符语义 | ✗（仅解析为 Modifier，无后端） |
| 继承 `extends`、`super`、`implements` | ✗ |
| 原型链 / 方法查找 | ✗ |
| `instanceof` | ✗ |
| `new` / 实例化 | ✗ |
| `enum` | ✗ |

---

## 7. 类型系统未实现（仅解析、不检查）

类型语法被解析为 AST 后在绑定 / 代码生成阶段**整体擦除**，不做任何类型检查：

- 类型注解、返回类型、类型参数、类型别名、接口、泛型约束等：解析 ✓，检查 ✗。
- 检查器诊断码（`TypeMismatch` `NotCallable` `PropertyNotFound` `ArgumentCountMismatch`）已定义但**未使用**。
- `as` / `satisfies` / 非空断言 `!`：直接擦除，不做任何断言语义。
- 泛型：无运行时实例化，类型参数被忽略。
- 可选链 `?.` 的类型窄化：无。

---

## 8. 运行时 / 语义与标准不符（已知偏差）

这些特性**能编译、能运行**，但结果不完全符合 ECMAScript：

| 项 | 偏差 |
| --- | --- |
| 异常处理 | `xt_throw` 只打印 `Uncaught …` 后 `exit(1)`，无真正可捕获的异常对象 / 栈；`try/catch` 未实现 |
| `for...in` | 与 `for...of` 共用同一实现：按数值下标 `0..length` 遍历并用 `xt_get` 取值；对对象无法枚举键（`xt_array_length(obj)` 返回 `undefined`），对数组仅得到数值下标 |
| 可选链 `?.` | `optional` 标志被忽略，退化为普通成员 / 调用访问，无空值短路语义 |
| 字符串 `length` | 运行时按 UTF-8 字节 / 码点计数，而非 JS 的 UTF-16 码元长度（emoji、非 BMP 字符长度会偏小） |
| BigInt | 字面量被 `Number()` 转成 double，失去任意精度 |
| 数字转字符串 | 仅覆盖常见情况（整数、最短往返），边界格式（科学计数法细节等）与 JS 不一致 |
| 宽松相等 `==` | 仅实现子集（number/string/bool/null/undefined），对象参与时按引用比较，未做 ToPrimitive |
| `+` 加法 | 数字 + 对象 / 数组等 ToPrimitive 路径不完整 |
| 数组越界 / 稀疏 | 越界访问返回 `undefined`，基本可用，但长度 / 稀疏语义与 JS 有差异 |
| 内存管理 | bump arena 永不释放，无 GC；长生命周期程序内存持续增长 |
| 函数 `arity` / 调用参数个数 | 无参数个数校验 |

---

## 9. 工具链 / 平台 / 工程未实现

| 项 | 现状 |
| --- | --- |
| GC（垃圾回收） | ✗ 有意推迟；`xt_alloc` 已隔离，但尚未替换为精确 / 保守回收器 |
| 自举（self-hosting） | ✗ 路线已规划（见 [DESIGN.MD](DESIGN.MD) / [README](../README.md)），尚未实现：运行时仍为 C，编译器自身尚未用 xbintsc 编译 |
| 类型检查器 | ✗ 仅定义诊断码，无 checker |
| 完整标准库（Math / JSON / Date / 集合等） | ✗ |
| 多文件模块打包 | ✗ |
| Windows 二进制产物验证 | 构建层已适配（`.exe` 后缀、链接参数分支），但需 CI 验证（`.github/workflows` 已配置） |
| 精确的 ECMAScript 数值 / 字符串 / 比较语义 | ✗ 见第 8 节 |

---

## 10. 速查：未实现 / 部分实现清单

```
未实现（语句）：class、new、this、enum、switch、try/catch/finally、
               namespace/module、import、标签语句 label:、export var/let/const

未实现（表达式）：正则字面量、标记模板、await、yield、对象展开、
                 delete、值级 typeof、值级 void、in、instanceof

未实现（函数）：默认参数、剩余参数、this 绑定、生成器、async/await、arguments

未实现（类/面向对象）：class、constructor、继承 extends/super、static、访问器、
                      原型链、new、instanceof

未实现（标准库）：Math、String 方法、Object 方法、JSON、Date、RegExp、
                 Map/Set、Promise、console.error/warn、parseInt/Number/String 等全局函数

未实现（数组方法）：pop、shift、unshift、join、slice、indexOf、includes、
                   map、forEach、filter、reduce（仅 push 已实现）

未实现（模块）：多文件、import/export 运行时语义、第三方依赖

未实现（类型系统）：类型检查、泛型实例化、断言语义、可选链类型窄化

未实现（运行时）：GC、可捕获异常、for-in 对象键枚举、?. 短路、UTF-16 length、
                 BigInt 精度、ToPrimitive 完整路径

未实现（工程）：自举、GC 替换、类型检查器、完整标准库
```
