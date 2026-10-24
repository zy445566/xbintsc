# 语义检查

> 属于[已实现的语法与功能](../implemented.md) · [English](../../implemented/checker.md)

位置：`src/checker/`（`checker.ts`、`undefined.ts`、`declarations.ts`、
`calls.ts`），由 `generate()` 在绑定之后、生成 LLVM IR 之前调用。

检查器**默认开启**；`--no-check`（或编程接口里的 `check: false`）可以关闭。
它只报告无需类型推断就能确定的事实，因此每条诊断都是真正的错误，而不是猜测：

| 诊断 | 错误码 | 检查内容 |
| --- | --- | --- |
| `Cannot find name` | TS3002 | 标识符既没有声明，也不是运行时全局 |
| `Duplicate identifier` | TS3001 | 同作用域内不能合并的重复声明（同一作用域两次 `let x`、`function f` 后又 `const f`、`let` 与参数同名……） |
| `Argument count mismatch` | TS4004 | 直接调用本地可见的函数或 `const` 绑定的箭头/函数表达式时，实参过少或过多 |

## 哪些名字算全局

已知全局集合直接取自代码生成使用的同一批表
（`src/codegen/generator/tables.ts`）：`console`、`Math`、`JSON`、`parseInt`、
定时器、`fetch`、各种构造函数、类型化数组、`Buffer`/`process` 等 Node 命名空间，
以及已启用扩展注册的全局（`print`、`__registerScript`、原生 manifest）。
往表里加一个名字，检查器在同一个提交里就会认可它，两者不会漂移。

类型位置不算值引用：`x as Foo`、`x satisfies Foo` 以及 `f<Foo>()` 的类型参数
都会被擦除，不会被报告为未定义。

`node_modules` 下被打包的源码不检查。检查器是针对你写的代码的门禁；依赖可能
合法地使用宿主全局，或使用启发式看不见的调用模式，因为依赖内部而让构建失败，
会让默认开启的检查器无法与 npm 包共存。

## 刻意不检查什么

这不是类型检查器。它不做可赋值性、类型收窄、泛型实例化或控制流分析，
也不校验被擦除的类型语法：

- `const x: number = "hello"` 仍然可以编译；类型注解只是文档。
- `as` / `satisfies` / 非空 `!` 在运行时仍然没有任何效果。
- `TypeMismatch`（TS4001）、`NotCallable`（TS4002）与 `PropertyNotFound`
  （TS4003）仍然没有使用。
- 通过属性调用（`object.method()`）、导入、会被重新赋值的 `let`/`var`
  绑定，以及展开实参的调用都会跳过：它们的签名不是静态事实。

## 实参个数规则

必需参数个数是第一个可选参数、默认参数或剩余参数之前的参数数量；
`this:` 参数不占实参位置。剩余参数会取消上限。重载/合并声明会被跳过，
因为不存在唯一权威的签名；函数体里引用 `arguments` 的函数也会跳过，
因为参数列表并不是它的完整签名。

同名导入可以重复：打包会把所有模块合并进同一个作用域，不同模块经常从同一个
内置模块导入 `readFileSync` 或 `dirname`。但本地声明仍然不能与导入同名。

## 关闭检查后的行为

`--no-check` 恢复旧的宽松流水线：类型语法被擦除后继续生成代码，适合需要
故意用不同实参个数调用函数的兼容场景。它也会恢复静默的 `Duplicate identifier`
行为：`let value = 1; let value = 2;` 可以编译，并且第一个值生效。
建议保持检查开启。

## 缓存与诊断

`check` 是增量缓存 key 的一部分，因此 `--no-check` 的构建产物不会与已检查的
产物互相复用。检查开启时，代码生成不再重复报告自己的 `Cannot find name`，
每个名字只报一次。`xbintsc check` 会运行检查并在链接前停止，是编辑器和
pre-commit 钩子的快速门禁。
