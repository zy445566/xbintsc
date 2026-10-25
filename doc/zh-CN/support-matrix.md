# 支持矩阵

> 语言：[English](../support-matrix.md) | **简体中文**

> 本文件由 `npm run docs:matrix` 根据可执行语料
> [`tests/conformance/test262/manifest.ts`](../../tests/conformance/test262/manifest.ts) 生成，请勿手工编辑：
> 修改用例后重新生成。

每个“支持”用例都是 [test262](https://github.com/tc39/test262) 的原始文件
（固定版本 `d53ebc8b1931d9f1541e43d59650776e4b766248`），由 xbintsc 编译**并执行**；
[test262 测试套件](../../tests/conformance/test262.test.ts) 会在用例回归时失败。该语料是代表性
子集，不是完整的 test262 全量运行。每个“不支持”用例都是
xbintsc 目前会拒绝的合法 JavaScript，并以实际产生的诊断码固定下来。
harness 的适配说明见 [`SOURCE.md`](../../tests/conformance/test262/SOURCE.md)。该套件在 CI 的每个平台
运行，因此本表不会声称编译器做不到的能力。

## 各领域覆盖

| 领域 | 支持 | 不支持 | 参考 |
| --- | ---: | ---: | --- |
| 值与运算符 | 2 | 1 | [参考](../implemented/operators.md) |
| 字符串（UTF-16 语义） | 2 | 0 | [参考](../implemented/strings.md) |
| 数组 | 2 | 0 | [参考](../implemented/stdlib.md) |
| 对象 | 2 | 0 | [参考](../implemented/stdlib.md) |
| 类与继承 | 2 | 0 | [参考](../implemented/codegen.md) |
| 闭包与函数 | 2 | 1 | [参考](../implemented/binder.md) |
| 异常 | 2 | 0 | [参考](../implemented/runtime-core.md) |
| 异步与 Promise | 2 | 0 | [参考](../implemented/async.md) |
| JSON | 2 | 0 | [参考](../implemented/stdlib.md) |
| Map 与 Set | 2 | 0 | [参考](../implemented/collections.md) |
| 正则表达式 | 2 | 0 | [参考](../implemented/stdlib.md) |
| 类型化数组与二进制 | 2 | 0 | [参考](../implemented/typed-arrays.md) |
| Date | 2 | 0 | [参考](../implemented/stdlib.md) |
| 生成器 | 2 | 0 | [参考](../implemented/async.md) |
| 模块 | 2 | 1 | [参考](../implemented/modules.md) |
| 可选链与模板字符串 | 2 | 1 | [参考](../implemented/operators.md) |

## 支持的用例（test262，编译并执行）

| test262 用例 | 领域 | 固化的行为 |
| --- | --- | --- |
| `built-ins/Number/isInteger/integers.js` | 值与运算符 | Number.isInteger 接受所有整数（含 ±2^53） |
| `built-ins/Number/isSafeInteger/infinity.js` | 值与运算符 | Number.isSafeInteger 拒绝 ±Infinity |
| `built-ins/String/prototype/padStart/fill-string-empty.js` | 字符串（UTF-16 语义） | 填充串为空时 padStart 原样返回 |
| `built-ins/String/prototype/includes/String.prototype.includes_Success.js` | 字符串（UTF-16 语义） | includes 在指定位置查找子串 |
| `built-ins/Array/prototype/map/15.4.4.19-1-1.js` | 数组 | map 以 undefined 为接收者时抛 TypeError |
| `built-ins/Array/prototype/at/index-non-numeric-argument-tointeger.js` | 数组 | at 把 false、null、undefined 和空串转换为索引 0 |
| `built-ins/Object/keys/15.2.3.14-0-1.js` | 对象 | Object.keys 是可调用函数 |
| `built-ins/Object/assign/Override.js` | 对象 | Object.assign 用后一个源对象覆盖同名键 |
| `language/statements/class/subclass/class-definition-evaluation-empty-constructor-heritage-present.js` | 类与继承 | 派生类未写构造函数时把参数转发给 super |
| `language/statements/class/definition/class-method-returns-promise.js` | 类与继承 | 类的 async 方法返回 Promise 实例 |
| `language/expressions/arrow-function/cannot-override-this-with-thisArg.js` | 闭包与函数 | 箭头函数的 this 不能被 thisArg 覆盖 |
| `language/expressions/function/S10.1.1_A1_T2.js` | 闭包与函数 | 具名、匿名和括号包裹的函数表达式都能求值 |
| `language/statements/try/12.14-10.js` | 异常 | catch 为名字查找引入独立作用域 |
| `built-ins/Error/length.js` | 异常 | Error 构造函数的 length 为 1 |
| `language/expressions/await/async-await-interleaved.js` | 异步与 Promise | await 与 promise 在微任务队列上的交错顺序 |
| `built-ins/Promise/all/S25.4.4.1_A1.1_T1.js` | 异步与 Promise | Promise.all 可调用 |
| `built-ins/JSON/stringify/property-order.js` | JSON | JSON.stringify 按属性顺序输出键 |
| `built-ins/JSON/parse/15.12.1.1-0-9.js` | JSON | JSON.parse 接受 token 前后的空白 |
| `built-ins/Map/prototype/set/append-new-values.js` | Map 与 Set | Map.set 按插入顺序追加条目 |
| `built-ins/Set/prototype/add/does-not-have-setdata-internal-slot-array.js` | Map 与 Set | Set.prototype.add 拒绝非 Set 接收者 |
| `built-ins/RegExp/prototype/test/S15.10.6.3_A1_T1.js` | 正则表达式 | test() 用 /1|12/ 匹配字符串 "123" |
| `built-ins/String/prototype/replace/15.5.4.11-1.js` | 正则表达式 | replace 给替换回调传入正确的 this |
| `built-ins/DataView/prototype/getUint8/index-is-out-of-range.js` | 类型化数组与二进制 | 越过视图末尾时 getUint8 抛 RangeError |
| `built-ins/DataView/prototype/setUint32/no-value-arg.js` | 类型化数组与二进制 | 省略 value 参数时 setUint32 写入 0 |
| `built-ins/Date/prototype/getUTCFullYear/this-value-valid-date.js` | Date | getUTCFullYear 读取有效日期 |
| `built-ins/Date/parse/year-zero.js` | Date | Date.parse 拒绝 -000000 扩展年份 |
| `language/statements/generators/declaration.js` | 生成器 | 支持生成器函数声明 |
| `language/expressions/generators/default-proto.js` | 生成器 | 生成器函数共享内置生成器原型 |
| `language/module-code/eval-export-cls-semi.js` | 模块 | 导出的 class 声明不需要末尾分号 |
| `language/module-code/eval-export-fun-semi.js` | 模块 | 导出的函数声明不需要末尾分号 |
| `language/expressions/optional-chaining/call-expression.js` | 可选链与模板字符串 | 可选链作用于调用表达式 |
| `language/expressions/template-literal/evaluation-order.js` | 可选链与模板字符串 | 模板插值从左到右求值 |

## 不支持的用例（合法 JavaScript，预期诊断）

| test262 用例 | 领域 | 诊断码 | 原因 |
| --- | --- | ---: | --- |
| `language/statements/with/12.10-0-1.js` | 可选链与模板字符串 | TS2001 | `with` 语句尚未实现 |
| `language/expressions/new.target/value-via-call.js` | 闭包与函数 | TS4005 | `new.target` 尚未实现 |
| `language/expressions/comma/S11.14_A2.1_T3.js` | 值与运算符 | TS3002 | 未绑定的名字是编译期错误，而不是运行时 `ReferenceError` |
| `language/expressions/dynamic-import/always-create-new-promise.js` | 模块 | TS2001 | 动态 `import()` 尚未实现 |
