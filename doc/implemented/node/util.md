# `util`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/util.md)

Location: `src/extensions/node/util/index.ts`, `runtime/ext_node/util/util.c`

Both named imports (`import { format } from "util"`) and namespace calls
(`import util from "util"` / `import * as util from "util"`) are supported.

| Function | Notes |
| --- | --- |
| `format(fmt, ...args)` | `%s` `%d` `%i` `%f` `%j` `%o` `%O` `%c` `%%` placeholders |
| `formatWithOptions(opts, fmt, ...args)` | options accepted and ignored |
| `inspect(value)` | recursive printer (depth-limited, strings quoted) |
| `isDeepStrictEqual(a, b)` | structural comparison (`NaN` equals `NaN`) |
| `inherits(ctor, superCtor)` | prototype wiring |
| `deprecate(fn, msg)` | returns `fn` unchanged (there is no warning channel) |
| `promisify(fn)` | wraps a trailing-callback function into a `Promise` |
| `isString` `isNumber` `isBoolean` `isUndefined` `isNull` `isFunction` `isArray` `isObject` `isBuffer` `isDate` `isRegExp` `isPromise` `isError` | type predicates |

```ts
import { format, promisify } from "util";

console.log(format("%s=%d", "n", 3)); // n=3
```
