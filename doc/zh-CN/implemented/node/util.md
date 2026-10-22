# `util` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/util.md)

位置：`src/extensions/node/util/index.ts`、`runtime/ext_node/util/util.c`

同时支持命名导入（`import { format } from "util"`）与命名空间调用
（`import util from "util"` / `import * as util from "util"`）。

| 函数 | 说明 |
| --- | --- |
| `format(fmt, ...args)` | 支持 `%s` `%d` `%i` `%f` `%j` `%o` `%O` `%c` `%%` 占位符 |
| `formatWithOptions(opts, fmt, ...args)` | 接受选项但忽略 |
| `inspect(value)` | 递归打印（限制深度，字符串带引号） |
| `isDeepStrictEqual(a, b)` | 结构比较（`NaN` 等于 `NaN`） |
| `inherits(ctor, superCtor)` | 连接原型链 |
| `deprecate(fn, msg)` | 原样返回 `fn`（没有告警通道） |
| `promisify(fn)` | 把「回调在末尾」的函数包装为 `Promise` |
| `isString` `isNumber` `isBoolean` `isUndefined` `isNull` `isFunction` `isArray` `isObject` `isBuffer` `isDate` `isRegExp` `isPromise` `isError` | 类型判断 |

```ts
import { format, promisify } from "util";

console.log(format("%s=%d", "n", 3)); // n=3
```
