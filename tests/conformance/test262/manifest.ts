/**
 * The test262 cases that define the supported subset.
 *
 * Every `path` under `cases/` is an unmodified file from the pinned test262
 * checkout recorded in `SOURCE.md`. `test262.test.ts` compiles and executes
 * each supported case; a case that starts failing fails the suite instead of
 * silently widening the claim. Unsupported cases are valid JavaScript that
 * xbintsc currently rejects, pinned with the diagnostic code it produces, so a
 * newly supported construct is noticed when its case starts compiling.
 *
 * `scripts/support-matrix.ts` renders this catalogue into
 * `doc/support-matrix.md`; a test keeps the two in sync.
 */

import { DiagnosticCode } from "../../../src/diagnostics/diagnostic.js";

export interface SupportArea {
  readonly id: string;
  readonly title: string;
  readonly titleZh: string;
  /** Documentation page that owns the behaviour. */
  readonly doc: string;
}

export const SUPPORT_AREAS = [
  { id: "values", title: "Values and operators", titleZh: "值与运算符", doc: "doc/implemented/operators.md" },
  { id: "strings", title: "Strings (UTF-16 semantics)", titleZh: "字符串（UTF-16 语义）", doc: "doc/implemented/strings.md" },
  { id: "arrays", title: "Arrays", titleZh: "数组", doc: "doc/implemented/stdlib.md" },
  { id: "objects", title: "Objects", titleZh: "对象", doc: "doc/implemented/stdlib.md" },
  { id: "classes", title: "Classes and inheritance", titleZh: "类与继承", doc: "doc/implemented/codegen.md" },
  { id: "functions", title: "Closures and functions", titleZh: "闭包与函数", doc: "doc/implemented/binder.md" },
  { id: "errors", title: "Exceptions", titleZh: "异常", doc: "doc/implemented/runtime-core.md" },
  { id: "async", title: "Async and promises", titleZh: "异步与 Promise", doc: "doc/implemented/async.md" },
  { id: "json", title: "JSON", titleZh: "JSON", doc: "doc/implemented/stdlib.md" },
  { id: "collections", title: "Map and Set", titleZh: "Map 与 Set", doc: "doc/implemented/collections.md" },
  { id: "regexp", title: "Regular expressions", titleZh: "正则表达式", doc: "doc/implemented/stdlib.md" },
  { id: "binary", title: "Typed arrays and binary data", titleZh: "类型化数组与二进制", doc: "doc/implemented/typed-arrays.md" },
  { id: "date", title: "Date", titleZh: "Date", doc: "doc/implemented/stdlib.md" },
  { id: "generators", title: "Generators", titleZh: "生成器", doc: "doc/implemented/async.md" },
  { id: "modules", title: "Modules", titleZh: "模块", doc: "doc/implemented/modules.md" },
  { id: "syntax", title: "Optional chaining and templates", titleZh: "可选链与模板字符串", doc: "doc/implemented/operators.md" },
] as const satisfies readonly SupportArea[];

export type SupportAreaId = (typeof SUPPORT_AREAS)[number]["id"];

export interface SupportedCase {
  /** Path relative to `tests/conformance/test262/cases/`. */
  readonly path: string;
  readonly area: SupportAreaId;
  readonly claim: string;
  readonly claimZh: string;
}

export interface UnsupportedCase extends SupportedCase {
  readonly code: DiagnosticCode;
}

export const SUPPORTED_CASES: readonly SupportedCase[] = [
  {
    path: "built-ins/Number/isInteger/integers.js",
    area: "values",
    claim: "Number.isInteger accepts every integer, including ±2^53",
    claimZh: "Number.isInteger 接受所有整数（含 ±2^53）",
  },
  {
    path: "built-ins/Number/isSafeInteger/infinity.js",
    area: "values",
    claim: "Number.isSafeInteger rejects ±Infinity",
    claimZh: "Number.isSafeInteger 拒绝 ±Infinity",
  },
  {
    path: "built-ins/String/prototype/padStart/fill-string-empty.js",
    area: "strings",
    claim: "padStart leaves the string unchanged when the fill string is empty",
    claimZh: "填充串为空时 padStart 原样返回",
  },
  {
    path: "built-ins/String/prototype/includes/String.prototype.includes_Success.js",
    area: "strings",
    claim: "includes finds a substring at the requested position",
    claimZh: "includes 在指定位置查找子串",
  },
  {
    path: "built-ins/Array/prototype/map/15.4.4.19-1-1.js",
    area: "arrays",
    claim: "map throws a TypeError when its receiver is undefined",
    claimZh: "map 以 undefined 为接收者时抛 TypeError",
  },
  {
    path: "built-ins/Array/prototype/at/index-non-numeric-argument-tointeger.js",
    area: "arrays",
    claim: "at coerces false, null, undefined and the empty string to index 0",
    claimZh: "at 把 false、null、undefined 和空串转换为索引 0",
  },
  {
    path: "built-ins/Object/keys/15.2.3.14-0-1.js",
    area: "objects",
    claim: "Object.keys is a callable function",
    claimZh: "Object.keys 是可调用函数",
  },
  {
    path: "built-ins/Object/assign/Override.js",
    area: "objects",
    claim: "Object.assign lets a later source override an earlier key",
    claimZh: "Object.assign 用后一个源对象覆盖同名键",
  },
  {
    path: "language/statements/class/subclass/class-definition-evaluation-empty-constructor-heritage-present.js",
    area: "classes",
    claim: "a derived class without a constructor forwards its arguments to super",
    claimZh: "派生类未写构造函数时把参数转发给 super",
  },
  {
    path: "language/statements/class/definition/class-method-returns-promise.js",
    area: "classes",
    claim: "an async class method returns a promise instance",
    claimZh: "类的 async 方法返回 Promise 实例",
  },
  {
    path: "language/expressions/arrow-function/cannot-override-this-with-thisArg.js",
    area: "functions",
    claim: "arrow functions cannot have this overridden by thisArg",
    claimZh: "箭头函数的 this 不能被 thisArg 覆盖",
  },
  {
    path: "language/expressions/function/S10.1.1_A1_T2.js",
    area: "functions",
    claim: "named, anonymous and parenthesised function expressions all evaluate",
    claimZh: "具名、匿名和括号包裹的函数表达式都能求值",
  },
  {
    path: "language/statements/try/12.14-10.js",
    area: "errors",
    claim: "catch introduces its own scope for name lookup",
    claimZh: "catch 为名字查找引入独立作用域",
  },
  {
    path: "built-ins/Error/length.js",
    area: "errors",
    claim: "the Error constructor reports length 1",
    claimZh: "Error 构造函数的 length 为 1",
  },
  {
    path: "language/expressions/await/async-await-interleaved.js",
    area: "async",
    claim: "await and promise continuations interleave on the microtask queue",
    claimZh: "await 与 promise 在微任务队列上的交错顺序",
  },
  {
    path: "built-ins/Promise/all/S25.4.4.1_A1.1_T1.js",
    area: "async",
    claim: "Promise.all is callable",
    claimZh: "Promise.all 可调用",
  },
  {
    path: "built-ins/JSON/stringify/property-order.js",
    area: "json",
    claim: "JSON.stringify emits keys in property order",
    claimZh: "JSON.stringify 按属性顺序输出键",
  },
  {
    path: "built-ins/JSON/parse/15.12.1.1-0-9.js",
    area: "json",
    claim: "JSON.parse accepts whitespace before and after a token",
    claimZh: "JSON.parse 接受 token 前后的空白",
  },
  {
    path: "built-ins/Map/prototype/set/append-new-values.js",
    area: "collections",
    claim: "Map.set appends new entries in insertion order",
    claimZh: "Map.set 按插入顺序追加条目",
  },
  {
    path: "built-ins/Set/prototype/add/does-not-have-setdata-internal-slot-array.js",
    area: "collections",
    claim: "Set.prototype.add rejects a non-Set receiver",
    claimZh: "Set.prototype.add 拒绝非 Set 接收者",
  },
  {
    path: "built-ins/RegExp/prototype/test/S15.10.6.3_A1_T1.js",
    area: "regexp",
    claim: "test() applies /1|12/ to the string \"123\"",
    claimZh: "test() 用 /1|12/ 匹配字符串 \"123\"",
  },
  {
    path: "built-ins/String/prototype/replace/15.5.4.11-1.js",
    area: "regexp",
    claim: "replace passes the correct this to the replacer function",
    claimZh: "replace 给替换回调传入正确的 this",
  },
  {
    path: "built-ins/DataView/prototype/getUint8/index-is-out-of-range.js",
    area: "binary",
    claim: "getUint8 throws a RangeError past the end of the view",
    claimZh: "越过视图末尾时 getUint8 抛 RangeError",
  },
  {
    path: "built-ins/DataView/prototype/setUint32/no-value-arg.js",
    area: "binary",
    claim: "setUint32 stores 0 when the value argument is omitted",
    claimZh: "省略 value 参数时 setUint32 写入 0",
  },
  {
    path: "built-ins/Date/prototype/getUTCFullYear/this-value-valid-date.js",
    area: "date",
    claim: "getUTCFullYear reads a valid date",
    claimZh: "getUTCFullYear 读取有效日期",
  },
  {
    path: "built-ins/Date/parse/year-zero.js",
    area: "date",
    claim: "Date.parse rejects the -000000 extended year",
    claimZh: "Date.parse 拒绝 -000000 扩展年份",
  },
  {
    path: "language/statements/generators/declaration.js",
    area: "generators",
    claim: "generator function declarations are supported",
    claimZh: "支持生成器函数声明",
  },
  {
    path: "language/expressions/generators/default-proto.js",
    area: "generators",
    claim: "generator functions share the intrinsic generator prototype",
    claimZh: "生成器函数共享内置生成器原型",
  },
  {
    path: "language/module-code/eval-export-cls-semi.js",
    area: "modules",
    claim: "an exported class declaration needs no trailing semicolon",
    claimZh: "导出的 class 声明不需要末尾分号",
  },
  {
    path: "language/module-code/eval-export-fun-semi.js",
    area: "modules",
    claim: "an exported function declaration needs no trailing semicolon",
    claimZh: "导出的函数声明不需要末尾分号",
  },
  {
    path: "language/expressions/optional-chaining/call-expression.js",
    area: "syntax",
    claim: "optional chaining applies to call expressions",
    claimZh: "可选链作用于调用表达式",
  },
  {
    path: "language/expressions/template-literal/evaluation-order.js",
    area: "syntax",
    claim: "template substitutions evaluate from left to right",
    claimZh: "模板插值从左到右求值",
  },
];

export const UNSUPPORTED_CASES: readonly UnsupportedCase[] = [
  {
    path: "language/statements/with/12.10-0-1.js",
    area: "syntax",
    claim: "`with` statements are not implemented",
    claimZh: "`with` 语句尚未实现",
    code: DiagnosticCode.UnexpectedToken,
  },
  {
    path: "language/expressions/new.target/value-via-call.js",
    area: "functions",
    claim: "`new.target` has no lowering",
    claimZh: "`new.target` 尚未实现",
    code: DiagnosticCode.UnsupportedFeature,
  },
  {
    path: "language/expressions/comma/S11.14_A2.1_T3.js",
    area: "values",
    claim: "an unbound name is a compile-time error, not a runtime `ReferenceError`",
    claimZh: "未绑定的名字是编译期错误，而不是运行时 `ReferenceError`",
    code: DiagnosticCode.CannotFindName,
  },
  {
    path: "language/expressions/dynamic-import/always-create-new-promise.js",
    area: "modules",
    claim: "dynamic `import()` is not implemented",
    claimZh: "动态 `import()` 尚未实现",
    code: DiagnosticCode.UnexpectedToken,
  },
];
