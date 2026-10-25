# Support matrix

> Language: **English** | [简体中文](./zh-CN/support-matrix.md)

> **Generated** by `npm run docs:matrix` from the executable catalogue in
> [`tests/conformance/test262/manifest.ts`](../tests/conformance/test262/manifest.ts). Do not edit this
> file by hand: change a case and regenerate.

Every supported case is an unmodified [test262](https://github.com/tc39/test262)
file (pinned at `d53ebc8b1931d9f1541e43d59650776e4b766248`) that xbintsc compiles **and runs**; the
[test262 suite](../tests/conformance/test262.test.ts) fails if one stops passing. The corpus is a
representative subset, not a full test262 run. Every unsupported case
is valid JavaScript that xbintsc currently rejects, pinned with the diagnostic
code it produces. See [`SOURCE.md`](../tests/conformance/test262/SOURCE.md) for the harness adaptation.
Because the suite runs on every CI platform, this table cannot claim more than
the compiler does.

## Coverage by area

| Area | Supported | Unsupported | Reference |
| --- | ---: | ---: | --- |
| Values and operators | 2 | 1 | [reference](implemented/operators.md) |
| Strings (UTF-16 semantics) | 2 | 0 | [reference](implemented/strings.md) |
| Arrays | 2 | 0 | [reference](implemented/stdlib.md) |
| Objects | 2 | 0 | [reference](implemented/stdlib.md) |
| Classes and inheritance | 2 | 0 | [reference](implemented/codegen.md) |
| Closures and functions | 2 | 1 | [reference](implemented/binder.md) |
| Exceptions | 2 | 0 | [reference](implemented/runtime-core.md) |
| Async and promises | 2 | 0 | [reference](implemented/async.md) |
| JSON | 2 | 0 | [reference](implemented/stdlib.md) |
| Map and Set | 2 | 0 | [reference](implemented/collections.md) |
| Regular expressions | 2 | 0 | [reference](implemented/stdlib.md) |
| Typed arrays and binary data | 2 | 0 | [reference](implemented/typed-arrays.md) |
| Date | 2 | 0 | [reference](implemented/stdlib.md) |
| Generators | 2 | 0 | [reference](implemented/async.md) |
| Modules | 2 | 1 | [reference](implemented/modules.md) |
| Optional chaining and templates | 2 | 1 | [reference](implemented/operators.md) |

## Supported cases (test262, compiled and executed)

| test262 case | Area | What it pins down |
| --- | --- | --- |
| `built-ins/Number/isInteger/integers.js` | Values and operators | Number.isInteger accepts every integer, including ±2^53 |
| `built-ins/Number/isSafeInteger/infinity.js` | Values and operators | Number.isSafeInteger rejects ±Infinity |
| `built-ins/String/prototype/padStart/fill-string-empty.js` | Strings (UTF-16 semantics) | padStart leaves the string unchanged when the fill string is empty |
| `built-ins/String/prototype/includes/String.prototype.includes_Success.js` | Strings (UTF-16 semantics) | includes finds a substring at the requested position |
| `built-ins/Array/prototype/map/15.4.4.19-1-1.js` | Arrays | map throws a TypeError when its receiver is undefined |
| `built-ins/Array/prototype/at/index-non-numeric-argument-tointeger.js` | Arrays | at coerces false, null, undefined and the empty string to index 0 |
| `built-ins/Object/keys/15.2.3.14-0-1.js` | Objects | Object.keys is a callable function |
| `built-ins/Object/assign/Override.js` | Objects | Object.assign lets a later source override an earlier key |
| `language/statements/class/subclass/class-definition-evaluation-empty-constructor-heritage-present.js` | Classes and inheritance | a derived class without a constructor forwards its arguments to super |
| `language/statements/class/definition/class-method-returns-promise.js` | Classes and inheritance | an async class method returns a promise instance |
| `language/expressions/arrow-function/cannot-override-this-with-thisArg.js` | Closures and functions | arrow functions cannot have this overridden by thisArg |
| `language/expressions/function/S10.1.1_A1_T2.js` | Closures and functions | named, anonymous and parenthesised function expressions all evaluate |
| `language/statements/try/12.14-10.js` | Exceptions | catch introduces its own scope for name lookup |
| `built-ins/Error/length.js` | Exceptions | the Error constructor reports length 1 |
| `language/expressions/await/async-await-interleaved.js` | Async and promises | await and promise continuations interleave on the microtask queue |
| `built-ins/Promise/all/S25.4.4.1_A1.1_T1.js` | Async and promises | Promise.all is callable |
| `built-ins/JSON/stringify/property-order.js` | JSON | JSON.stringify emits keys in property order |
| `built-ins/JSON/parse/15.12.1.1-0-9.js` | JSON | JSON.parse accepts whitespace before and after a token |
| `built-ins/Map/prototype/set/append-new-values.js` | Map and Set | Map.set appends new entries in insertion order |
| `built-ins/Set/prototype/add/does-not-have-setdata-internal-slot-array.js` | Map and Set | Set.prototype.add rejects a non-Set receiver |
| `built-ins/RegExp/prototype/test/S15.10.6.3_A1_T1.js` | Regular expressions | test() applies /1|12/ to the string "123" |
| `built-ins/String/prototype/replace/15.5.4.11-1.js` | Regular expressions | replace passes the correct this to the replacer function |
| `built-ins/DataView/prototype/getUint8/index-is-out-of-range.js` | Typed arrays and binary data | getUint8 throws a RangeError past the end of the view |
| `built-ins/DataView/prototype/setUint32/no-value-arg.js` | Typed arrays and binary data | setUint32 stores 0 when the value argument is omitted |
| `built-ins/Date/prototype/getUTCFullYear/this-value-valid-date.js` | Date | getUTCFullYear reads a valid date |
| `built-ins/Date/parse/year-zero.js` | Date | Date.parse rejects the -000000 extended year |
| `language/statements/generators/declaration.js` | Generators | generator function declarations are supported |
| `language/expressions/generators/default-proto.js` | Generators | generator functions share the intrinsic generator prototype |
| `language/module-code/eval-export-cls-semi.js` | Modules | an exported class declaration needs no trailing semicolon |
| `language/module-code/eval-export-fun-semi.js` | Modules | an exported function declaration needs no trailing semicolon |
| `language/expressions/optional-chaining/call-expression.js` | Optional chaining and templates | optional chaining applies to call expressions |
| `language/expressions/template-literal/evaluation-order.js` | Optional chaining and templates | template substitutions evaluate from left to right |

## Unsupported cases (valid JavaScript, expected diagnostics)

| test262 case | Area | Diagnostic | Why |
| --- | --- | ---: | --- |
| `language/statements/with/12.10-0-1.js` | Optional chaining and templates | TS2001 | `with` statements are not implemented |
| `language/expressions/new.target/value-via-call.js` | Closures and functions | TS4005 | `new.target` has no lowering |
| `language/expressions/comma/S11.14_A2.1_T3.js` | Values and operators | TS3002 | an unbound name is a compile-time error, not a runtime `ReferenceError` |
| `language/expressions/dynamic-import/always-create-new-promise.js` | Modules | TS2001 | dynamic `import()` is not implemented |
