/**
 * Differential tests: compile a program with xbintsc, run it, and compare the
 * output byte-for-byte with the same program executed by Node. Every program
 * prints canonical `JSON.stringify` lines so console formatting never masks a
 * real semantic difference.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("differential vs Node", (harness) => {
  /** Register one differential case from a complete program source. */
  const diff = (name: string, source: string): void => {
    it(name, () => {
      harness.expectSameOutputAsNode(source, { name: `diff_${name.replace(/\W+/g, "_")}` });
    });
  };

  const printAll = (values: string[]): string =>
    `const values: unknown[] = [\n${values.map((v) => `  ${v},`).join("\n")}\n];\n` +
    `for (const v of values) console.log(JSON.stringify(v));\n`;

  diff(
    "number formatting and coercion",
    printAll([
      "0",
      "-0",
      "1.5",
      "0.1",
      "1 / 3",
      "1e21",
      "1e-7",
      "1e-6",
      "123456789012345680000",
      "Number.MAX_SAFE_INTEGER",
      "Number.MIN_SAFE_INTEGER",
      "Number.EPSILON",
      "Infinity",
      "-Infinity",
      "NaN",
      "(1234.5678).toFixed(2)",
      "(0.000001).toFixed(7)",
      "(1e21).toFixed(2)",
      "(2.5).toFixed(0)",
      "(-2.5).toFixed(0)",
      "(123.456).toPrecision(5)",
      "(123.4).toPrecision(10)",
      "(0.0001234).toPrecision(2)",
      "(12345).toExponential()",
      "(12345).toExponential(2)",
      "(0.000123).toExponential(3)",
      "(255).toString(16)",
      "(255).toString(2)",
      "String(42)",
      "String(1e21)",
      "String(1e-7)",
    ]),
  );

  diff(
    "number parsing and predicates",
    printAll([
      'Number("  42  ")',
      'Number("")',
      'Number("0x1f")',
      'Number("0b101")',
      'Number("0o17")',
      'Number("abc")',
      'parseInt("42px")',
      'parseFloat("3.14abc")',
      "isNaN(NaN)",
      "isFinite(3)",
      "Number.isInteger(3)",
      "Number.isInteger(3.5)",
      "Number.isNaN(NaN)",
      "Number.isFinite(Infinity)",
      "Number.isSafeInteger(2 ** 53)",
    ]),
  );

  diff(
    "abstract equality and coercion",
    printAll([
      '1 == "1"',
      '1 === "1"',
      "0 == false",
      '"" == false',
      "null == undefined",
      "null === undefined",
      "[] == false",
      "[] == 0",
      "[1] == 1",
      '[1, 2] == "1,2"',
      '{} == "[object Object]"',
      "NaN == NaN",
      '"1" == true',
      '"0" == false',
    ]),
  );

  diff(
    "arithmetic and relational operators",
    printAll([
      "[] + []",
      "[1] + [2]",
      "[1, 2] + [3]",
      '{} + ""',
      '1 + "2"',
      '"3" - 1',
      '"3" * "2"',
      "true + 1",
      "null + 1",
      "undefined + 1",
      '+ "42"',
      '+ "0x1f"',
      '+ ""',
      '+ "  12  "',
      '+ "abc"',
      "[2] < 3",
      '[1, 2] < "1,3"',
      '"10" < "9"',
      "10 < 9",
      "2 ** 10",
      "7 % 3",
      "-7 % 3",
      "1 / 0",
      "0 / 0",
    ]),
  );

  diff(
    "logical operators, bitwise and typeof",
    printAll([
      '0 || "x"',
      '"" || "y"',
      'null ?? "z"',
      'undefined ?? "w"',
      "0 ?? 5",
      "5 & 3",
      "5 | 2",
      "5 ^ 1",
      "~5",
      "1 << 4",
      "-8 >> 1",
      "-8 >>> 1",
      "typeof 1",
      'typeof "a"',
      "typeof true",
      "typeof undefined",
      "typeof null",
      "typeof {}",
      "typeof []",
      "typeof (() => 1)",
    ]),
  );

  diff(
    "string methods",
    printAll([
      '"Hello, World".length',
      '"Hello, World".toUpperCase()',
      '"Hello, World".toLowerCase()',
      '"Hello, World".indexOf("o")',
      '"Hello, World".lastIndexOf("o")',
      '"Hello, World".includes("World")',
      '"Hello, World".slice(7)',
      '"Hello, World".slice(-5)',
      '"Hello, World".substring(0, 5)',
      '"Hello, World".substr(7, 3)',
      '"a, b, c".split(", ")',
      '"Hello".replace("l", "L")',
      '"Hello".replaceAll("l", "L")',
      '"ab".repeat(3)',
      '"Hello".charAt(1)',
      '"Hello".charCodeAt(0)',
      '"  pad  ".trim()',
      '"ab".padStart(5, "-")',
      '"ab".padEnd(5, "-")',
      '"Hello".concat("!" )',
      '"a1b2c3".match(/\\d/g)',
      '"a1b2c3".replace(/\\d/g, "#")',
      '"abc".at(-1)',
      '"Hello".localeCompare("World")',
    ]),
  );

  diff(
    "array methods",
    printAll([
      "[3, 1, 2].sort()",
      '["banana", "apple", "cherry"].sort()',
      "[10, 9, 100].sort()",
      "[3, 1, 2].sort((a, b) => a - b)",
      "[1, 2, 3].map((x) => x * 2)",
      "[1, 2, 3, 4].filter((x) => x % 2 === 0)",
      "[1, 2, 3].reduce((a, b) => a + b, 0)",
      "[1, 2, 3].reverse()",
      '[1, 2, 3].join("-")',
      "[1, [2, [3, [4]]]].flat(2)",
      "[1, 2, 3].flatMap((x) => [x, x])",
      "[1, 2, 3, 4].slice(1, 3)",
      "[1, 2, 3].concat([4, 5])",
      "[1, 2, 3].indexOf(2)",
      "[1, 2, 3].includes(4)",
      "[5, 3, 8].find((x) => x > 4)",
      "[1, 2, 3].some((x) => x > 2)",
      "[1, 2, 3].every((x) => x > 0)",
      "Array.isArray([])",
      "Array.from([1, 2, 3], (x) => x + 1)",
      "Array.of(1, 2, 3)",
      "[1, 2, 3].fill(0, 1)",
      "[1, 2, 3, 4].copyWithin(0, 2)",
      "[1, 2, 3].at(-1)",
      "[1, 2, 3].splice(1, 1)",
    ]),
  );

  diff(
    "objects, key order and JSON",
    `const o = { b: 2, a: 1, "10": "ten", "2": "two", c: 3 };
${printAll([
  "Object.keys(o)",
  "Object.values(o)",
  "Object.entries(o)",
  "JSON.stringify(o)",
  "JSON.stringify({ x: undefined, y: null, z: () => 1 })",
  "JSON.stringify([undefined, null, () => 1])",
  "JSON.stringify({ a: { b: [1, 2, { c: 3 }] } })",
  'JSON.stringify("hi")',
  "JSON.stringify(5)",
  "JSON.stringify(true)",
  "JSON.stringify(null)",
  "JSON.stringify(NaN)",
  "JSON.stringify(Infinity)",
  "JSON.stringify(undefined)",
  "Object.assign({}, o, { d: 4 })",
  "{ ...o, e: 5 }",
  '"a" in o',
  'o.hasOwnProperty("a")',
])}`,
  );

  diff(
    "control flow, closures and classes",
    `function fib(n: number): number { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function classify(x: number): string {
  if (x < 0) return "neg";
  else if (x === 0) return "zero";
  return "pos";
}
function sum(...xs: number[]): number { return xs.reduce((a, b) => a + b, 0); }
function defaults(a: number, b = 10, c = a + b): number { return a + b + c; }
function safeDiv(a: number, b: number): string {
  try {
    if (b === 0) throw new Error("divide by zero");
    return String(a / b);
  } catch (err) {
    return "caught";
  } finally {
    // always runs
  }
}
class Animal {
  name: string;
  constructor(name: string) { this.name = name; }
  speak(): string { return this.name + " makes a sound"; }
  static create(n: string): Animal { return new Animal(n); }
}
class Dog extends Animal {
  constructor(name: string) { super(name); }
  speak(): string { return this.name + " barks"; }
}
const counter = () => { let c = 0; return () => ++c; };
const mk = counter();
const results: unknown[] = [
  fib(10), classify(-5), classify(0), classify(5), sum(1, 2, 3),
  defaults(1), defaults(1, 2), defaults(1, 2, 3),
  mk(), mk(), mk(),
  new Animal("Cat").speak(), new Dog("Rex").speak(), Animal.create("Bird").speak(),
  Dog.prototype instanceof Animal,
  safeDiv(10, 2), safeDiv(1, 0),
  (() => { let s = 0; for (let i = 0; i < 5; i++) s += i; return s; })(),
  (() => { let s = ""; for (const ch of ["a", "b", "c"]) s += ch; return s; })(),
  (() => { let i = 0, s = 0; while (i < 3) { s += i; i++; } return s; })(),
  (() => { let i = 0; do { i++; } while (i < 3); return i; })(),
  (() => { switch (2) { case 1: return "one"; case 2: return "two"; default: return "other"; } })(),
];
for (const v of results) console.log(JSON.stringify(v));`,
  );

  diff(
    "Math, collections and dates",
    printAll([
      "Math.max(1, 2, 3)",
      "Math.min()",
      "Math.abs(-3)",
      "Math.floor(1.9)",
      "Math.ceil(1.1)",
      "Math.round(2.5)",
      "Math.round(-2.5)",
      "Math.trunc(-1.9)",
      "Math.sign(-5)",
      "Math.sqrt(16)",
      "Math.pow(2, 8)",
      "Math.PI",
      "new Date(0).toISOString()",
      "new Date(1000).getTime()",
      "new Date(2020, 0, 1).getUTCFullYear()",
      "new Date(Date.UTC(2020, 5, 15)).toISOString()",
      "[...new Set([1, 2, 2, 3])]",
      "[...new Map([[\"a\", 1], [\"b\", 2]]).keys()]",
    ]),
  );

  diff(
    "regexp capture groups and string splitting",
    printAll([
      '"12-34".match(/(\\d+)-(\\d+)/)',
      '"a1b2c3".split(/(\\d)/)',
      '"a1b2c3".split(/\\d/)',
      '"2020-01-02".replace(/(\\d+)-(\\d+)-(\\d+)/, "$3/$2/$1")',
      '"a1b2".replace(/(\\d)/g, "[$1]")',
      '"a.b.c".search(/\\./)',
      '"abc".split(/(?:)/)',
      '"hello world".match(/(\\w+) (\\w+)/)',
      '"aaa".split("a", 2)',
      '"a,b;c".split(/[,;]/)',
    ]),
  );

  diff(
    "immutable array methods and Object statics",
    printAll([
      "[3, 1, 2].toSorted((x, y) => x - y)",
      "[3, 1, 2].toReversed()",
      "[3, 1, 2].toSpliced(1, 1, 9, 9)",
      "[3, 1, 2].with(1, 9)",
      "[3, 1, 2].with(-1, 9)",
      "Object.getOwnPropertyNames({ b: 1, a: 2 })",
      "Object.getOwnPropertyNames([1, 2])",
      "Object.groupBy([1, 2, 3, 4], (n) => (n % 2 === 0 ? \"even\" : \"odd\"))",
      "Array.from({ length: 3 }, (_v, i) => i * 2)",
    ]),
  );

  diff(
    "tagged templates and String.raw",
    `function tag(strings: TemplateStringsArray, ...values: unknown[]): string {
  let out = strings[0];
  for (let i = 0; i < values.length; i++) out += "[" + String(values[i]) + "]" + strings[i + 1];
  return out;
}
const obj = {
  prefix: "<",
  tag(strings: TemplateStringsArray, ...values: unknown[]): string {
    return this.prefix + strings.join("|") + "/" + JSON.stringify(strings.raw) + "/" + values.join(",");
  },
};
const name = "world";
${printAll([
  'tag`hello ${name} and ${1 + 1}!`',
  'tag`no substitutions`',
  'obj.tag`a\\n${name}b${2}`',
  'String.raw`a\\nb\\t${name}c`',
  'String.raw`plain`',
  'tag`${name}`',
])}`,
  );

  diff(
    "Function.prototype.call, apply and bind",
    `function add(a: number, b: number): number { return a + b; }
const obj = {
  base: 10,
  sum(a: number, b: number): number { return this.base + a + b; },
};
class Greeter {
  prefix = "hi";
  greet(name: string): string { return this.prefix + " " + name; }
}
const g = new Greeter();
const bound = obj.sum.bind(obj, 1);
const bound2 = add.bind(null, 10);
const mk = (x: number) => x * 2;
${printAll([
  "add.call(null, 1, 2)",
  "obj.sum.call(obj, 1, 2)",
  "obj.sum.apply(obj, [3, 4])",
  "add.apply(null, [5, 6])",
  "bound(2)",
  "bound2(5)",
  'g.greet.call(g, "ada")',
  'g.greet.apply(g, ["bob"])',
  'g.greet.bind(g)("cy")',
  "mk.call(null, 21)",
  "mk.apply(null, [3])",
])}`,
  );

  diff(
    "first-class built-in method values",
    `const array = [1, 2, 3];
const text = "abc";
const fn = function (x: number): number { return x; };
const date = new Date(0);
const map = new Map<string, number>();
map.set("a", 1);
const set = new Set<number>([1, 2]);
const regexp = /a(b)c/;
const object = { base: 10, add(n: number): number { return this.base + n; } };
type Holder = { base: number; go?: (n: number) => number };
const holder: Holder = { base: 10, go(n: number): number { return this.base + n; } };
const empty: { go?: (n: number) => number } = {};
const results: unknown[] = [
  typeof array.map,
  typeof array.push,
  typeof text.slice,
  typeof fn.call,
  typeof date.getTime,
  typeof map.get,
  typeof set.add,
  typeof regexp.test,
  array.map.name,
  array.map.length,
  text.slice.length,
  array.map(function (x: number): number { return x * 2; }),
  array.map?.((x: number): number => x + 1),
  text.split?.(""),
  object.add(5),
  holder.go?.(3),
  empty.go?.(3),
];
for (const value of results) console.log(JSON.stringify(value));
try {
  const detached = array.map;
  (detached as unknown as (cb: (x: number) => number) => number[])((x: number): number => x);
  console.log(JSON.stringify("no throw"));
} catch (caught) {
  console.log(JSON.stringify("threw"));
}
`,
  );

  diff(
    "object-literal getters and setters",
    `const obj = {
  _x: 0,
  get x(): number { return this._x; },
  set x(v: number) { this._x = v * 2; },
  get double(): number { return this._x * 2; },
  method(a: number, b: number): number { return a + b; },
};
obj.x = 5;
const base = 7;
const o2 = { base, get next() { return base + 1; } };
const o3 = { get ["dyn" + "amic"]() { return 42; } };
${printAll([
  "obj.x",
  "obj.double",
  "obj.method(2, 3)",
  "Object.keys(obj)",
  "o2.base",
  "o2.next",
  "o3.dynamic",
])}`,
  );

  it("binds namespace imports to every export", () => {
    harness.expectSameOutputAsNode(
      `import * as util from "./util.ts";
import { add } from "./util.ts";
const values: unknown[] = [
  util.greeting,
  util.add(2, 3),
  add(20, 22),
  new util.default().value,
  Object.keys(util).sort(),
  typeof util.add,
];
for (const v of values) console.log(JSON.stringify(v));
`,
      {
        name: "diff_namespace_import",
        files: {
          "util.ts": [
            'export const greeting = "hello";',
            "export function add(a: number, b: number): number { return a + b; }",
            "export default class Thing { value = 42; }",
            "export const ignored = 1;\n",
          ].join("\n"),
        },
      },
    );
  });
});
