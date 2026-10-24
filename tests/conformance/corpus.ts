/**
 * The executable definition of "supported": one case per behaviour the
 * documentation claims.
 *
 * Supported cases are compiled by xbintsc **and** run under Node; the runner
 * asserts byte-identical stdout, so a case is evidence of agreement, not of a
 * hand-written expectation. Unsupported cases must fail with the listed
 * diagnostic code. `scripts/support-matrix.ts` renders this catalogue into
 * `doc/support-matrix.md`, and a test keeps the two in sync.
 */

import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

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
  readonly name: string;
  readonly area: SupportAreaId;
  readonly claim: string;
  readonly claimZh: string;
  readonly source: string;
  /** Extra modules written next to the entry, keyed by relative path. */
  readonly files?: Readonly<Record<string, string>>;
}

export interface UnsupportedCase {
  readonly name: string;
  readonly area: SupportAreaId;
  readonly claim: string;
  readonly claimZh: string;
  readonly source: string;
  readonly code: DiagnosticCode;
}

export const SUPPORTED_CASES: readonly SupportedCase[] = [
  {
    name: "numeric-boundaries",
    area: "values",
    claim: "IEEE-754 special values, precision limits and safe-integer checks",
    claimZh: "IEEE-754 特殊值、精度边界与安全整数判断",
    source: `
console.log(1 / 0, -1 / 0, 0 / 0, 1 / -0);
console.log(0.1 + 0.2, 2 ** 53, 2 ** 53 + 1);
console.log(Number.isInteger(2 ** 53), Number.isSafeInteger(2 ** 53 + 1));
`,
  },
  {
    name: "bigint",
    area: "values",
    claim: "BigInt literals keep full precision and describe themselves correctly",
    claimZh: "BigInt 字面量保持完整精度，typeof/toString 行为正确",
    source: `
const big = 9007199254740993n;
console.log(big + 1n, big * 2n, typeof big, big.toString());
`,
  },
  {
    name: "coercion-and-equality",
    area: "values",
    claim: "`+`, relational and loose equality coerce like ECMAScript",
    claimZh: "`+`、比较和宽松相等按 ECMAScript 规则转换",
    source: `
console.log(1 + "2", "5" * 2, [] + {}, null == undefined, 0 == false, NaN === NaN);
console.log((true && "x") || "y", null ?? "fallback");
`,
  },
  {
    name: "utf16-length-and-search",
    area: "strings",
    claim: "length/indexOf/code points follow UTF-16, not bytes",
    claimZh: "length/indexOf/码点按 UTF-16 计算，而不是按字节",
    source: `
const text = "中文😀a";
console.log(text.length, text.indexOf("😀"), Array.from(text).length);
console.log("x".padStart(3, "0"), "a,b,c".split(",").join("|"), "abc".slice(1));
`,
  },
  {
    name: "array-pipeline",
    area: "arrays",
    claim: "map/filter/reduce/at/spread and destructuring compose",
    claimZh: "map/filter/reduce/at/展开与解构可以组合使用",
    source: `
const values = [1, 2, 3, 4];
console.log(values.map((v) => v * 2).join(","), values.filter((v) => v % 2 === 0).join(","));
console.log(values.reduce((a, b) => a + b, 0), values.at(-1), values.includes(3));
const [first, ...rest] = values;
console.log(first, rest.length, [...values, 5].join(","));
`,
  },
  {
    name: "object-literal-and-spread",
    area: "objects",
    claim: "computed keys, methods, `this` and object spread preserve shape",
    claimZh: "计算属性、方法、`this` 与对象展开保持对象形状",
    source: `
const key = "b";
const object = { a: 1, [key]: 2, sum() { return this.a + this.b; } };
console.log(Object.keys(object).join(","), object.sum());
const clone = { ...object, c: 3 };
console.log(clone.a, clone.b, clone.c);
`,
  },
  {
    name: "classes-and-super",
    area: "classes",
    claim: "fields, constructors, statics, inheritance, super and instanceof",
    claimZh: "字段、构造函数、静态成员、继承、super 与 instanceof",
    source: `
class Animal {
  name: string;
  constructor(name: string) { this.name = name; }
  speak(): string { return this.name + " speaks"; }
  static kind = "animal";
}
class Dog extends Animal {
  speak(): string { return super.speak() + " woof"; }
}
const dog = new Dog("Rex");
console.log(dog.speak(), Dog.kind, dog instanceof Animal, dog.name);
`,
  },
  {
    name: "closures-and-defaults",
    area: "functions",
    claim: "captured locals survive, defaults and rest parameters work",
    claimZh: "捕获的局部变量可存活，默认参数与剩余参数可用",
    source: `
function counter() { let count = 0; return () => ++count; }
const next = counter();
console.log(next(), next(), next());
function sum(a: number, b = 2, ...rest: number[]) { return a + b + rest.length; }
console.log(sum(1), sum(1, 3, 4, 5));
`,
  },
  {
    name: "try-catch-finally",
    area: "errors",
    claim: "subclassed errors, catch binding and finally ordering",
    claimZh: "Error 子类、catch 绑定与 finally 执行顺序",
    source: `
class AppError extends Error {}
function run() {
  try { throw new AppError("boom"); }
  catch (error) { return (error as Error).message; }
  finally { console.log("finally"); }
}
console.log(run(), new AppError("x") instanceof AppError);
`,
  },
  {
    name: "await-ordering",
    area: "async",
    claim: "await suspends, queues a microtask and preserves interleaving with sync code",
    claimZh: "await 会挂起并按微任务排队，与同步代码的交错顺序正确",
    source: `
async function main() {
  console.log("start");
  await null;
  console.log("after-await");
  const values = await Promise.all([Promise.resolve(1), 2]);
  console.log(values.join(","));
}
main();
console.log("sync");
`,
  },
  {
    name: "json-round-trip",
    area: "json",
    claim: "stringify/parse round-trips nested values and key order",
    claimZh: "stringify/parse 往返嵌套值并保持键顺序",
    source: `
const text = JSON.stringify({ a: [1, 2], b: "x" });
const parsed = JSON.parse(text);
console.log(text, parsed.a[1], parsed.b);
`,
  },
  {
    name: "map-and-set",
    area: "collections",
    claim: "insertion order and identity-based deduplication",
    claimZh: "插入顺序与基于同一性的去重",
    source: `
const map = new Map<string, number>();
map.set("a", 1);
map.set("b", 2);
console.log(map.size, map.get("a"), [...map.keys()].join(","));
const set = new Set<number>();
set.add(1); set.add(2); set.add(2);
console.log(set.size, [...set].join(","));
`,
  },
  {
    name: "regexp-match-replace",
    area: "regexp",
    claim: "capture groups, test and global replacement on ASCII text",
    claimZh: "ASCII 文本上的捕获组、test 与全局替换",
    source: `
const match = /(\\d+)-(\\d+)/.exec("x 12-34 y");
console.log(match ? match[1] + ":" + match[2] : "none");
console.log(/ab+c/.test("abbbc"), "a1b2".replace(/\\d/g, "#"));
`,
  },
  {
    name: "typed-array-and-dataview",
    area: "binary",
    claim: "Uint8Array indexing, iteration and DataView byte access",
    claimZh: "Uint8Array 索引/迭代与 DataView 字节访问",
    source: `
const bytes = new Uint8Array([1, 2, 3]);
console.log(bytes.length, bytes[1], Array.from(bytes).join(","));
const view = new DataView(new ArrayBuffer(4));
view.setUint32(0, 0x01020304);
console.log(view.getUint8(0), view.byteLength);
`,
  },
  {
    name: "date-utc",
    area: "date",
    claim: "UTC construction, ISO formatting and parsing agree",
    claimZh: "UTC 构造、ISO 格式化与解析结果一致",
    source: `
const date = new Date(Date.UTC(2020, 0, 2, 3, 4, 5));
console.log(date.toISOString(), date.getUTCFullYear(), Date.parse("2020-01-02T03:04:05.000Z"));
`,
  },
  {
    name: "generator-protocol",
    area: "generators",
    claim: "yield, spread and manual next() drive the same generator",
    claimZh: "yield、展开与手动 next() 驱动同一个生成器",
    source: `
function* numbers() { yield 1; yield 2; yield 3; }
console.log([...numbers()].join(","));
const iterator = numbers();
console.log(iterator.next().value, iterator.next().value);
`,
  },
  {
    name: "esm-imports",
    area: "modules",
    claim: "default, named and namespace imports across bundled modules",
    claimZh: "跨打包模块的默认、命名与命名空间导入",
    source: `
import double, { add } from "./math.ts";
import * as math from "./math.ts";
console.log(add(1, 2), double(3), math.add(4, 5));
`,
    files: {
      "math.ts": `
export function add(a: number, b: number) { return a + b; }
export default function double(value: number) { return value * 2; }
`,
    },
  },
  {
    name: "optional-chain-and-template",
    area: "syntax",
    claim: "optional chaining, nullish fallback and template substitution",
    claimZh: "可选链、空值合并回退与模板插值",
    source: `
const object: { a?: { b: number } } = {};
console.log(object?.a?.b ?? "missing");
const name = "world";
console.log(\`hello \${name}\`, true ? "yes" : "no");
`,
  },
];

export const UNSUPPORTED_CASES: readonly UnsupportedCase[] = [
  {
    name: "namespace-declaration",
    area: "modules",
    claim: "`namespace` is parsed but not lowered",
    claimZh: "`namespace` 可以解析，但不会生成代码",
    source: "namespace N { export const x = 1; }",
    code: DiagnosticCode.UnsupportedFeature,
  },
  {
    name: "new-target",
    area: "functions",
    claim: "`new.target` has no lowering",
    claimZh: "`new.target` 尚未实现",
    source: "function f() { new.target; }",
    code: DiagnosticCode.UnsupportedFeature,
  },
  {
    name: "angle-bracket-assertion",
    area: "syntax",
    claim: "`<T>value` assertions are rejected; use `as T`",
    claimZh: "拒绝 `<T>value` 断言，请改用 `as T`",
    source: "const value = <number>1;",
    code: DiagnosticCode.UnexpectedToken,
  },
  {
    name: "with-statement",
    area: "syntax",
    claim: "`with` is rejected by the parser",
    claimZh: "解析器拒绝 `with` 语句",
    source: "with ({}) {}",
    code: DiagnosticCode.UnexpectedToken,
  },
];
