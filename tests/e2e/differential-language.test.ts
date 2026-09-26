/**
 * Differential tests for control flow, classes, regexes, generators and function semantics.
 */

import { describeE2E } from "./harness.js";
import { makeDifferential } from "./differential-helpers.js";

describeE2E("differential vs Node: language and built-ins", (harness) => {
  const { diff, printAll } = makeDifferential(harness);

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

  diff(
    "Error family and AggregateError",
    `
const e = new Error("boom");
class MyErr extends Error {
  constructor(m: string) { super(m); this.name = "MyErr"; }
}
class CustomRange extends RangeError {}
const m = new MyErr("m");
const r = new CustomRange("too big");
const agg = new AggregateError([1, 2], "all failed");
${printAll([
  "typeof Error",
  "typeof TypeError",
  "typeof AggregateError",
  "e instanceof Error",
  "new TypeError('b') instanceof TypeError",
  "new TypeError('b') instanceof Error",
  "new RangeError('c') instanceof TypeError",
  "String(e)",
  "e.toString()",
  "e.name",
  "e.message",
  "m.name",
  "m.message",
  "m instanceof MyErr",
  "m instanceof Error",
  "m instanceof TypeError",
  "r.name",
  "r.message",
  "r instanceof RangeError",
  "r instanceof Error",
  "agg.name",
  "agg.message",
  "agg.errors",
  "agg instanceof AggregateError",
  "agg instanceof Error",
  "String(agg)",
  "typeof e.stack",
])}`,
  );

  diff(
    "Promise.any rejects with an AggregateError",
    `
Promise.any([Promise.reject("a"), Promise.reject("b"), Promise.resolve(42)])
  .then((value) => console.log(JSON.stringify(["fulfilled", value])))
  .catch((reason) => {
    const err = reason as { name: string; message: string; errors: unknown[] };
    console.log(JSON.stringify([err.name, err.message, err.errors]));
  });
Promise.any([Promise.reject("x"), Promise.reject("y")]).catch((reason) => {
  const err = reason as { name: string; errors: unknown[] };
  console.log(JSON.stringify([err.name, err.errors, reason instanceof AggregateError]));
});
`,
  );

  diff(
    "generators",
    `
function* nums() { yield 1; yield 2; yield 3; }
const g = nums();
console.log(JSON.stringify([typeof nums, g.next(), g.next(), g.next(), g.next()]));

function* counter(start: number) {
  let x = start;
  while (true) {
    const step = yield x;
    x = step === undefined ? x + 1 : step;
  }
}
const c = counter(10);
console.log(JSON.stringify([c.next().value, c.next().value, c.next(100).value, c.next().value]));

function* inner() { yield "a"; return "done"; }
function* outer() { const r = yield* inner(); yield r; yield* [1, 2]; }
console.log(JSON.stringify([...outer()]));

function* echo() { const a = yield "first"; const b = yield a; return b; }
const e = echo();
console.log(JSON.stringify([e.next(), e.next("A"), e.next("B")]));

function* guarded() {
  try { yield "x"; } catch (err) { yield "caught:" + (err as Error).message; }
  yield "end";
}
const t = guarded();
console.log(JSON.stringify([t.next(), t.throw(new Error("E")), t.next(), t.next()]));

function* naturals() { let i = 0; while (true) yield i++; }
const seen: number[] = [];
for (const v of naturals()) { if (v >= 3) break; seen.push(v); }
console.log(JSON.stringify(seen));

class Box {
  private n = 0;
  *gen(): Generator<number> { yield this.n++; yield this.n++; }
}
console.log(JSON.stringify([...new Box().gen()]));

const obj = {
  base: 5,
  *gen() { yield this.base; yield this.base * 2; },
};
console.log(JSON.stringify([...obj.gen(), Math.max(...obj.gen())]));
`,
  );
});
