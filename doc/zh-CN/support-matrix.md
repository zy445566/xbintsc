# 支持矩阵

> 语言：[English](../support-matrix.md) | **简体中文**

> 本文件由 `npm run docs:matrix` 根据可执行语料
> [`tests/conformance/corpus.ts`](../../tests/conformance/corpus.ts) 生成，请勿手工编辑：
> 修改用例后重新生成。

每个“支持”用例都会由 xbintsc 编译并在 Node 下运行，
[一致性测试](../../tests/conformance/conformance.test.ts) 断言两者 stdout 逐字节相同；每个“不支持”用例
必须以表中列出的诊断码失败。该测试在 CI 的每个平台运行，因此本表不会
声称编译器做不到的能力。

## 各领域覆盖

| 领域 | 支持 | 不支持 | 参考 |
| --- | ---: | ---: | --- |
| 值与运算符 | 3 | 0 | [参考](../implemented/operators.md) |
| 字符串（UTF-16 语义） | 1 | 0 | [参考](../implemented/strings.md) |
| 数组 | 1 | 0 | [参考](../implemented/stdlib.md) |
| 对象 | 1 | 0 | [参考](../implemented/stdlib.md) |
| 类与继承 | 1 | 0 | [参考](../implemented/codegen.md) |
| 闭包与函数 | 1 | 1 | [参考](../implemented/binder.md) |
| 异常 | 1 | 0 | [参考](../implemented/runtime-core.md) |
| 异步与 Promise | 1 | 0 | [参考](../implemented/async.md) |
| JSON | 1 | 0 | [参考](../implemented/stdlib.md) |
| Map 与 Set | 1 | 0 | [参考](../implemented/collections.md) |
| 正则表达式 | 1 | 0 | [参考](../implemented/stdlib.md) |
| 类型化数组与二进制 | 1 | 0 | [参考](../implemented/typed-arrays.md) |
| Date | 1 | 0 | [参考](../implemented/stdlib.md) |
| 生成器 | 1 | 0 | [参考](../implemented/async.md) |
| 模块 | 1 | 1 | [参考](../implemented/modules.md) |
| 可选链与模板字符串 | 1 | 2 | [参考](../implemented/operators.md) |

## 支持的用例（与 Node 对比）

| 用例 | 领域 | 固化的行为 |
| --- | --- | --- |
| `numeric-boundaries` | 值与运算符 | IEEE-754 特殊值、精度边界与安全整数判断 |
| `bigint` | 值与运算符 | BigInt 字面量保持完整精度，typeof/toString 行为正确 |
| `coercion-and-equality` | 值与运算符 | `+`、比较和宽松相等按 ECMAScript 规则转换 |
| `utf16-length-and-search` | 字符串（UTF-16 语义） | length/indexOf/码点按 UTF-16 计算，而不是按字节 |
| `array-pipeline` | 数组 | map/filter/reduce/at/展开与解构可以组合使用 |
| `object-literal-and-spread` | 对象 | 计算属性、方法、`this` 与对象展开保持对象形状 |
| `classes-and-super` | 类与继承 | 字段、构造函数、静态成员、继承、super 与 instanceof |
| `closures-and-defaults` | 闭包与函数 | 捕获的局部变量可存活，默认参数与剩余参数可用 |
| `try-catch-finally` | 异常 | Error 子类、catch 绑定与 finally 执行顺序 |
| `await-ordering` | 异步与 Promise | await 会挂起并按微任务排队，与同步代码的交错顺序正确 |
| `json-round-trip` | JSON | stringify/parse 往返嵌套值并保持键顺序 |
| `map-and-set` | Map 与 Set | 插入顺序与基于同一性的去重 |
| `regexp-match-replace` | 正则表达式 | ASCII 文本上的捕获组、test 与全局替换 |
| `typed-array-and-dataview` | 类型化数组与二进制 | Uint8Array 索引/迭代与 DataView 字节访问 |
| `date-utc` | Date | UTC 构造、ISO 格式化与解析结果一致 |
| `generator-protocol` | 生成器 | yield、展开与手动 next() 驱动同一个生成器 |
| `esm-imports` | 模块 | 跨打包模块的默认、命名与命名空间导入 |
| `optional-chain-and-template` | 可选链与模板字符串 | 可选链、空值合并回退与模板插值 |

## 不支持的用例（预期诊断）

| 用例 | 领域 | 诊断码 | 原因 |
| --- | --- | ---: | --- |
| `namespace-declaration` | 模块 | TS4005 | `namespace` 可以解析，但不会生成代码 |
| `new-target` | 闭包与函数 | TS4005 | `new.target` 尚未实现 |
| `angle-bracket-assertion` | 可选链与模板字符串 | TS2001 | 拒绝 `<T>value` 断言，请改用 `as T` |
| `with-statement` | 可选链与模板字符串 | TS2001 | 解析器拒绝 `with` 语句 |
