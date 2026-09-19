# xbintsc Implemented Syntax and Features

This document was compiled by checking the source (`src/`, `runtime/`) and tests
(`tests/`) file by file, and lists only the syntax and features that are
**actually usable today**. Implementation locations are noted for traceability.

> Note: the compiler *parses* much more than it *generates code* for. Many
> TypeScript constructs can be parsed, and even bound, but the code generation
> stage reports `UnsupportedFeature`. Those are **not** listed here; see the
> [unimplemented document](./unimplemented.md).

> Language: **English** | [简体中文](./zh-CN/implemented.md)

---

## 1. Compilation pipeline (fully implemented)

```
source.ts
   │  lexer       src/lexer/scanner.ts + token.ts
   ▼
 tokens
   │  parser      src/parser/parser.ts  →  AST  src/ast/
   ▼
 AST
   │  binder      src/binder/binder.ts  →  scopes/symbols/closure capture
   ▼
 bound AST
   │  codegen     src/codegen/llvm.ts + values.ts  →  LLVM IR text
   ▼
 module.ll ── clang ──► module.o ──link──► executable
                                   ▲
                          runtime/*.c (C runtime, split by function)
```

- Front end, back end, runtime and extensions are fully decoupled.
- The flow is orchestrated by `src/driver/compiler.ts`: `read → parse → bind → IR → object file → link`.

---

## 2. Lexer (implemented)

Location: `src/lexer/scanner.ts`, `src/lexer/token.ts`

- Full token classification: identifiers, keywords, private identifiers, numbers, strings, templates, regex (scanning), all punctuation and operators.
- Full keyword table: `abstract any as asserts async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object package private protected public readonly return satisfies set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield`
- Numeric literals:
  - Decimal, `0x` hex, `0o` octal, `0b` binary
  - Underscore separators `1_000_000`
  - Fractions, exponents `1.5e3`
  - BigInt literals (`10n`, `0xFFn`)
- String literals:
  - Single / double quotes
  - Escapes: `\n \t \r \b \f \0 \\ \' \"`, `\xHH`, `\uHHHH`, `\u{...}`
- Template literals (lexer level): no-substitution templates, `TemplateHead`, `TemplateMiddle`, `TemplateTail`, with `${}` and escapes.
- Regex literal scanning (`/pattern/flags`), distinguishing division `/`.
- Private identifiers `#name`.
- Comments: line `//` and block `/* */` (with unterminated diagnostics).
- Newline and whitespace tracking (for ASI), BOM / CRLF normalization (`SourceFile`).
- Lexical diagnostics: unterminated string / template / comment, illegal character, illegal number, illegal escape.

---

## 3. Parser (implemented)

Location: `src/parser/parser.ts`, `src/ast/nodes.ts`

### 3.1 Statements

- Variable declarations: `var` / `let` / `const`, with multiple declarators `const a = 1, b = 2;`
- Function declarations (including parsing the `async` modifier and the generator `*` marker)
- `class` declarations / class expressions (constructor, fields, methods, `static`, `extends`)
- `if` / `else`
- `while`, `do...while`
- `for` (init, condition and increment may all be omitted)
- `for...of`, `for...in` (see the semantic limits in 3.5)
- `return`, `break`, `continue`, `throw`
- `switch` / `case` / `default` (including fall-through)
- `try` / `catch` / `finally` (catchable exceptions based on a runtime setjmp frame)
- `export var` / `export let` / `export const` (modifier parsed then erased)
- Block statement `{}`, empty statement `;`, `debugger;`
- Expression statements

### 3.2 Expressions

- All common operators with precedence / associativity (see section 5)
- Assignment expressions and all compound assignments (see section 5)
- Conditional (ternary) expressions `a ? b : c`
- Arrow functions `() => expr` / `() => { ... }` (including type parameters and return type annotations)
- Function expressions `function () {}` and named function expressions `function g() {}`
- Call expressions `f(...)`, member access `a.b`, element access `a[i]`
- Array literals `[1, 2]`, sparse array elision, array spread `[...a]`
- Object literals `{ a: 1 }`, shorthand properties `{ a }`, method shorthand `{ m() {} }`
- Template literal `${}` substitutions, tagged templates (parsed only, see the [unimplemented document](./unimplemented.md))
- Parenthesized expressions, `as` / `satisfies` / non-null assertion `!` (type erasure)
- Unary: `+ - ! ~ typeof void`, prefix / postfix `++ --`
- Optional chaining `?.` / `?.[]` / `?.()` (with nullish short-circuit semantics)
- `delete` expressions (delete an object property)

### 3.3 TypeScript type syntax (parsed only, structure preserved then erased)

- Type annotations, return type annotations, type parameters `<T>` and constraints `<T extends U>`, type parameter defaults
- Type references, qualified names `A.B`
- Unions `|`, intersections `&`, arrays `T[]`, tuples `[T, U]`, optional tuple members `T?`, rest tuple members `...T`
- Function types `(a: T) => U`, construct signatures `new () => T`
- Object type literals, property signatures, method signatures, index signatures `[k: string]: T`
- Conditional types `T extends U ? X : Y`, mapped types `{ [K in T]: U }`, `infer`
- Type operators `keyof`, `unique`, `readonly`
- `typeof` (type query), indexed access types `T[K]`
- Literal types, `this` types
- Type predicates `x is T` / `asserts x is T`
- Interfaces, type aliases, enums, namespace / module declarations
- The various forms of `import` / `export` (structural parsing)

### 3.4 Module syntax (structural parsing + driver bundling)

- `import default, { named } from "..."`, `import * as ns from "..."` (namespace imports of relative modules parsed only; extension modules such as `path` are supported), `import type`
- `export default`, `export { a as b }`, `export *`, `export =`
- Import attributes (`with` / `assert`)
- The **runtime semantics** of `import` / `export` are handled at the driver layer by `src/driver/modules.ts`: it recursively resolves relative dependencies, renames top-level symbols with a per-module prefix, rewrites references, merges into a single file and rebinds. Circular dependencies error out.

### 3.5 ASI

- Automatic Semicolon Insertion, based on `precededByLineBreak` / `}` / EOF.

---

## 4. Name binding and scopes (Binder, implemented)

Location: `src/binder/binder.ts`

- Scope kinds: module, function, block, `for`, `catch`
- Symbol kinds: `var` / `let` / `const` / `function` / `parameter` / `class` / `interface` / `type` / `enum` / `import` / `namespace`
- `var` and function declarations hoist to the function scope; `let` / `const` stay block-scoped
- Identifier → declaration resolution; unresolved identifier collection (`CannotFindName`)
- Closure capture analysis: outer variables referenced by an inner function are marked `captured` / `boxed`, and capture indices are threaded through intermediate closures
- Parameters are registered as local symbols; the self-name of a named function expression is registered as `const`
- Type-only declarations (interface / type alias) do not participate in value capture

---

## 5. Operators and assignment (codegen implemented)

Location: `src/codegen/llvm.ts` (`BINARY_RUNTIME`, `emitPrefix`, `emitPostfix`, `emitAssignment`)

### 5.1 Arithmetic

`+ - * / % **` (`**` is right-associative)

### 5.2 Comparison and equality

`< <= > >=`, `== !=` (loose equality), `=== !==` (strict equality)

### 5.3 Logical and short-circuit

`&& || ??` (with short-circuit evaluation), `!`

### 5.4 Bitwise

`& | ^ ~ << >> >>>`

### 5.5 Unary

`+ - ! ~`, prefix / postfix `++ --`

### 5.6 Assignment

`= += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??=`

### 5.7 Other expression operators

- Comma expression `,`
- `in` operator (`key in obj`, mapped to `xt_in`)
- `delete obj.key` / `delete obj[key]` (mapped to `xt_delete`)
- `instanceof` (mapped to `xt_instance_of`, walking the prototype chain)

> `typeof` / `void` are implemented as unary operators.

---

## 6. Value model and calling convention (implemented)

Location: `src/codegen/values.ts`, `runtime/rt.h`

- All JS values are unified as a 64-bit `xt_value` (NaN-boxing).
- Doubles are not boxed; other types are tagged pointers with a 16-bit tag in the high bits and a 48-bit payload.
- Tags: `undefined` / `null` / `false` / `true` / `string` / `object` / `array` / `function`.
- Uniform function ABI:

```c
xt_value fn(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
```

- `thisValue` is threaded as the first ABI parameter (exception-safe, supports nesting); arrow functions inherit `this` lexically from an extra environment slot.
- Closures thread captured variables through `env` (by reference, boxed); direct calls and closure calls share one code path.
- `xt_object` carries a prototype field, and property lookup walks the prototype chain; `xt_function` carries a `prototype` and a property bag (static members).

---

## 7. LLVM IR code generation (Codegen, implemented)

Location: `src/codegen/llvm.ts`

- Emits LLVM IR text (`.ll`); no custom register allocation (relies on `alloca` + mem2reg).
- Statement / block boundary values live in `alloca`; conditionals and short-circuits materialize into temporary slots instead of `phi`.
- Control flow: `if` / `while` / `do` / `for` / `for...of` / `for...in`, `switch`, `try/catch/finally`, `break` / `continue` / `return`.
  - `switch` tests each `case` with strict equality, executes on a hit, and falls through until `break`.
  - `try/catch/finally` is implemented with a runtime `setjmp` frame: `xt_try_enter` pushes, `setjmp` catches, `xt_throw` long-jumps. Functions containing `try` force local variables to stay in memory (inline-asm escape points) so values survive a long jump.
  - `for...in` reuses `xt_object_keys` to enumerate keys (arrays / strings yield string indices).
- Expressions:
  - Identifiers, numbers, BigInt (treated as numbers), strings, templates, booleans, `null`, `undefined`, `arguments`
  - Arithmetic / comparison / logical / short-circuit / conditional / bitwise / unary (including `typeof` `void`) / prefix-postfix increment-decrement / compound assignment / logical assignment
  - Array literals (including spread `[...]`), object literals (including shorthand / methods / object spread `{...obj}`)
  - Property access (with a `length` special case, `Math` constants), element access, calls
  - Optional chaining `?.` / `?.[]` / `?.()`: nullish check short-circuits to `undefined`
  - `delete`, `in`, `instanceof`
  - `this` (saved to the function's `%saved.this`; arrow functions read from an environment slot), `super` (`super.x` / `super(...)`), `new`, `await`
  - Closures (arrow functions / function expressions) and capture environment construction
- Classes: constructor closure + prototype object, stored in an LLVM global (`@class.<id>`); instance fields are initialized before the constructor body; `static` members live in the constructor's property bag; `extends` sets up the prototype chain; `super(...)` is invoked through a hidden `__ctor` on the prototype.
- `async`: wrapped with `xt_promise_resolve` before returning; `await` calls `xt_await` (drives the microtask queue and raises an exception on rejection).
- Standard library calls: `console.*`, `Math.*`, `Object.*`, array / string methods all go through `xt_call_method` / `xt_math_call` / `xt_object_*`; `JSON`/`Date`/`Map`/`Set`/`RegExp`/`Promise` statics and constructors go through their `xt_*` functions; global functions (`parseInt` etc.) go through `xt_parse_int` etc.
- Global string pool (`@.str.N` private constants, UTF-8 escaped).
- Builtin calls: `console.log` / `info` / `warn` / `error`, `Math.*`, `Object.*`, array / string methods, global functions, extension builtins (uniform `(argc, argv)` ABI).
- `main` entry point (returns 0, calls the module function, and runs `xt_drain_microtasks` before returning).
- Unsupported nodes uniformly report `UnsupportedFeature`; they never crash.

---

## 8. C runtime (Runtime, implemented)

Location: `runtime/xt_alloc.c`, `runtime/xt_values.c`, `runtime/xt_containers.c`,
`runtime/xt_stdlib.c`, `runtime/xt_stdlib2.c`, `runtime/xt_promise.c`, `runtime/xt_builtins.c`,
`runtime/xt_io.c`, sharing the private header `runtime/rt_internal.h`; the public ABI is in `runtime/rt.h`.

- Allocator: bump arena, `calloc`-backed, never frees (GC is isolated behind `xt_alloc`).
- Value construction: `xt_undefined/xt_null/xt_bool/xt_number/xt_string_new/xt_string_from_cstr`.
- Strings: UTF-8 storage, concatenation, equality comparison, formatted number-to-string.
- Type conversion: `xt_truthy`, `xt_to_number`, `xt_to_string`, `xt_typeof`.
- Arithmetic: `add/sub/mul/div/mod/pow/neg/pos`.
- Bitwise: `and/or/xor/not/shl/shr/ushr` (including `ToInt32` semantics).
- Comparison: `lt/le/gt/ge`, loose / strict equality, `not`, `is_nullish`.
- Objects: linear property list, `object_new/get/set/has/keys/values/entries/assign/spread`.
- Arrays: `array_new/get/set/push/length/spread`.
- Generic member access: `xt_get` / `xt_set` (dispatch over arrays / objects / strings).
- Standard library: `xt_call_method` (uniform dispatch of array / string methods and function properties on objects), `xt_math_call` (`Math.*` and constants), global functions `xt_parse_int/parse_float/is_nan/is_finite/number_ctor/string_ctor/boolean_ctor`.
- Operator helpers: `xt_in` (`in`), `xt_delete` (`delete`), `xt_rest_args` (rest parameters / `arguments`).
- Box: `box_new/get/set` (for closure-captured variables).
- Functions and closures: `arg`, `closure_new/call/env/arity`.
- Exceptions: `xt_try_enter` / `xt_try_exception` / `xt_try_leave` maintain the `setjmp` frame stack; `xt_throw` long-jumps to the nearest `try` when a frame exists, otherwise prints `Uncaught ...` and exits.
- Output: `xt_print/xt_println/xt_console_log/info/warn/error` (Node-style inspect: arrays `[ a, b ]`, objects `{ k: v }`; `info`/`log` to stdout, `warn`/`error` to stderr), plus `dir/trace/assert/count/countReset/group/groupEnd/table/time/timeEnd/timeLog`.
- Objects / functions: `xt_object` with a prototype chain, `xt_function` with a property bag (static members and `prototype`); `xt_new` (instantiation), `xt_instance_of` (prototype chain), `xt_object_freeze/is_frozen/from_entries`.
- Standard library extensions (`xt_stdlib2.c`): extended array / string / number / object methods; `Object` / `Array` / `Number` / `String` statics; `JSON.parse` / `JSON.stringify`; `Map` / `Set`; `Date` (`gmtime_r`); `RegExp` (POSIX ERE `regcomp`/`regexec` `test`/`exec`).
- Promise (`xt_promise.c`): synchronous microtask queue (`xt_microtasks`); `xt_promise_ctor/resolve/reject/static`, instance `then/catch/finally`; `xt_await` drives the queue until settle, and rejection raises via `xt_throw`; `xt_drain_microtasks` empties the queue at program exit.

---

## 9. Extension mechanism (Extensions, implemented)

Location: `src/extensions/registry.ts`, `src/extensions/node/`

- An extension is a plain object: `runtimeSources()` (extra C sources), `linkerFlags()` (extra link flags), `builtins()` (global identifier → runtime symbol, uniform `(argc, argv)` ABI) and `modules()` (import specifier → named exports / namespace).
- `ExtensionRegistry`: register / unregister / lookup / aggregate builtins, modules, runtime sources and link flags.
- The built-in core extension `core`: exposes `print` (mapped to `xt_println`), always registered.
- The Node extension `node`:
  - Modular organisation: `src/extensions/node/fs/` + `runtime/ext_node/fs/read_file.c`
  - Exposes importable modules (`fs`, `fs/promises`, `path`, `os`, `process`, …) under both bare and `node:`-prefixed specifiers; `import { readFileSync } from "fs"` resolves to `xt_node_read_text_file`, and `path`/`os`/`process` map exports onto the namespace dispatchers.
- Adding a new module only requires a new directory plus a C implementation; the core compiler never changes.

---

## 10. Driver, incremental compilation and toolchain (implemented)

Location: `src/driver/compiler.ts`, `src/driver/cache.ts`, `src/driver/toolchain.ts`, `src/driver/paths.ts`

- Compilation pipeline: read source → module bundling (`src/driver/modules.ts`, when the entry contains `import`/`export`) → parse → bind/check → IR → object file → link.
- Incremental cache: keyed on "compiler version + source hash + emit kind + optimization level + platform + extension set"; if the artifacts exist and are fresh, the build is skipped.
- C runtime and extension sources are cached as object files by content hash and compiled only once.
- Toolchain wrapper: locates `clang` (overridable with `xbintsc_CLANG`), compiles IR, compiles C, links.
- Link flags: `-lm` is added automatically on non-Windows; extensions may append extra link flags.

---

## 11. CLI and programmatic API (implemented)

Location: `src/cli/main.ts`, `src/index.ts`, `bin/xbintsc.js`

### 11.1 CLI commands

```
xbintsc build <file.ts> [options]   compile to a native binary
xbintsc run   <file.ts> [-- args]   compile and run
xbintsc emit  <file.ts>             print LLVM IR
xbintsc version                     print the version
xbintsc help                        help
```

### 11.2 CLI options

```
-o, --output <path>   Output path
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0..-O3              Optimization level (default: -O2)
    --ext <names>     Comma separated extensions (e.g. node)
    --force           Ignore the incremental cache
    --verbose         Print progress information
```

### 11.3 Programmatic API

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

---

## 12. Tests (implemented)

Location: `tests/` (`lexer` / `parser` / `binder` / `codegen` / `driver` / `extensions` / `cli` / `e2e`)

- Per-module unit tests; when `clang` is present, e2e really compiles and runs binaries, otherwise it is skipped automatically.
- e2e coverage: arithmetic and printing, recursive functions, loops / arrays / string concatenation, closures capturing by reference, JS-style printing of objects / arrays, the Node `fs` extension via `import`, `switch` fall-through, array / string methods, `Math` and global functions and all `console` levels, default / rest parameters and `arguments`, `Object` helpers and spread and `in`/`delete`, `for...in` object key enumeration, `try/catch/finally`, optional chaining, classes and `new`/`this`/`static`/`extends`/`super`/`instanceof`, `async`/`await` and `Promise`, `Map`/`Set`/`JSON` and extended standard library, multi-file `import`/`export`.

---

## 13. Implemented features quick reference

| Category | Contents |
| --- | --- |
| Declarations | `var` `let` `const`, function declarations, function expressions, arrow functions, `class` (declaration / expression), interfaces / type aliases (erased) |
| Control flow | `if/else`, `while`, `do...while`, `for`, `for...of`, `for...in`, `switch`, `try/catch/finally`, `break`, `continue`, `return`, `throw` |
| Expressions | Identifiers, literals, template strings, array / object literals (with spread), calls, member / element access, optional chaining, closures, `arguments`, `this`, `new`, `super`, `await` |
| Operators | Arithmetic, comparison, equality, logical, bitwise, shift, unary (including `typeof`/`void`), prefix/postfix increment-decrement, compound assignment, logical assignment, `in`, `delete`, `instanceof` |
| Functions | Default parameters, rest parameters, capturing closures, `this` binding, lexical `this` in arrow functions |
| Classes / OO | Constructors, instance fields, methods, `static`, inheritance `extends`/`super`, prototype chain, `instanceof` |
| Async | `async`/`await`, `Promise` (`then/catch/finally`, `resolve/reject/all/allSettled/race`), synchronous microtask queue |
| Modules | `import`/`export` (named / default / re-export / `export *`), multi-file bundling over relative paths, bare specifiers resolved to extension modules |
| Standard library | Array / string / number / object extension methods, `Math`, `JSON`, `Date`, `RegExp`, `Map`, `Set`, `Object/Array/Number/String` statics, `console.*` |
| Value model | 64-bit NaN-boxing, uniform function ABI (including `this`), closure environments, object prototype chains |
| Runtime | Strings / objects / arrays / closures / arithmetic / comparison / catchable exceptions / Promise / collections / `console` |
| Extensions | Extension registry, `core` (print), `node` (fs / path / os / process / buffer / stream / net / dgram / http imported by specifier) |
| Toolchain | clang compiles IR/C, linking, incremental cache |
| Platforms | macOS / Linux / Windows (adapted at the build level, CI in `.github/workflows`) |
