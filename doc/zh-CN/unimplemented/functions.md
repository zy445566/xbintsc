# 函数

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/functions.md)

| 特性 | 解析 | 语义 |
| --- | --- | --- |
| 默认参数 `function f(a = 5)` | ✓ | ✓ 已实现 |
| 剩余参数 `function f(...args)` | ✓ | ✓ 已实现 |
| 调用参数展开 `f(...args)` | ✓ | ✓ 已实现 |
| 解构参数 / 解构绑定 | ✓ | ✓ 已实现 |
| `arguments` 对象 | ✓（隐式） | ✓ 已实现（箭头函数取的是自身参数，而非外层函数的 `arguments`，与 JS 不同） |
| `this` 绑定 / 方法调用语义 | ✓ | ✓ 已实现（`this` 作为函数首个 ABI 参数线程化；箭头函数词法继承） |
| `async` / `await` / Promise | ✓ | ✓ 已实现：基于栈式协程的真实挂起；等待 pending promise 会暂停函数体并由事件循环恢复（剩余差异见偏差表） |
| `new.target` | ✗ | 已解析，但以 `UnsupportedFeature` 明确拒绝（无 `new.target` 语义） |
| 生成器 / 迭代器 / `yield` | ✓ 已实现 | `function*`、`yield`、`yield*`、`.next/.throw/.return`；`async function*`、`for await...of` 与 `Symbol.asyncIterator` 也已支持 |
| 闭包 `arity` | — | `xt_closure_arity` 恒为 -1，未填充 |
| `fn.call` / `fn.apply` / `fn.bind` | ✓ | ✓ 已实现（绑定闭包不跟踪部分参数的 `length`） |
| `fn.name` / `fn.length` | ✓ | ✓ 已实现（从声明 / 赋值 / 属性键推断；绑定函数为 `"bound ..."` 并调整 arity） |
| 内置方法一等公民（`typeof arr.map`、`const f = arr.push`、`obj.method?.()`） | ✓ | ✓ 作为**未绑定**方法值实现：读取 `arr.map` 得到函数，`arr.map?.(cb)` 与 `obj.method?.()` 会把接收者作为 `this` 绑定，脱离接收者调用（`const f = arr.map; f(cb)`）会像 JavaScript 一样抛错。方法的 `name`/`length` 也一并暴露 |
| 内建构造器一等公民（`const A = Array`、`new A(...)`、`typeof Map`） | ✓ | ✓ 构造器是普通函数值，带 `name`/`length`、`prototype`、静态成员（`A.isArray`）与完整的 `instanceof` 协议 |
| `fn.toString()` | ~ | 返回原生占位字符串（`function name() { [native code] }`），而非原始源码文本 |
