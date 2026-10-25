/**
 * Differential cases for comma (sequence) expressions, `async function`
 * expressions and class static blocks — syntax that used to be a parse error.
 */

import { describeE2E } from "./harness.js";
import { makeDifferential } from "./differential-helpers.js";

describeE2E("sequence expressions, async function expressions and static blocks", (harness) => {
  const { diff, printAll } = makeDifferential(harness);

  diff(
    "comma expressions evaluate left to right and yield the last value",
    printAll([
      `(1, 2, 3)`,
      `(() => { let a = 0; const v = (a += 1, a += 2, a); return [v, a]; })()`,
      `(() => { let a = 0, b = 0; const arr = [(a = 1, a), (b = 2, b)]; return [arr.join(","), a, b]; })()`,
      `(() => { let i = 0, j = 0; for (let k = 0, l = 3; k < l; k++, j += 2) i++; return [i, j]; })()`,
      `(() => { let a = 0; const f = (v: number): number => v; return f((a = 4, a)); })()`,
      `(() => { let a = 0; const g = (): number => (a++, a * 10); return [g(), a]; })()`,
      `(() => { let a = 0; if ((a = 3, a === 3)) return "ok"; return "no"; })()`,
      `(() => { const o = { k: (1, 2) }; return o.k; })()`,
      `(() => { class K { static m(): number { return 1; } } const k = new (0, K)(); return k instanceof K; })()`,
      `(() => { let sum = 0; for (const v of (0, [1, 2, 3])) sum += v; return sum; })()`,
      `(() => { let a = 0; const t = ((a = 1, a), 5); return [t, a]; })()`,
      `((): number => { let a = 0; return (a++, a++, a); })()`,
    ]),
  );

  diff(
    "async function expressions are callable and awaitable",
    `const double = async function (v: number): Promise<number> { return v * 2; };
const greet = async function named(name: string): Promise<string> { return "hi " + name; };
const gen = async function* (limit: number): AsyncGenerator<number> {
  for (let i = 0; i < limit; i++) yield i;
};
const results: unknown[] = [];
double(21).then((v) => { results.push(v); });
greet("xbintsc").then((v) => { results.push(v); });
results.push(typeof double, double.name, double.length);
console.log(results.join(" "));
const collect = async (): Promise<void> => {
  const seen: number[] = [];
  for await (const value of gen(3)) seen.push(value);
  console.log(seen.join(","));
};
collect();`,
  );

  diff(
    "class static blocks run once, in order, with `this` bound to the class",
    `const log: string[] = [];
class Config {
  static base = 10;
  static total = 0;
  static { log.push("start"); this.total = this.base + 1; }
  static { log.push("second:" + Config.total); Config.total += 1; }
  static #secret = 5;
  static { this.total += Config.#secret; }
  static make(): Config { return new Config(); }
}
class Base {
  static value = 7;
  static read(): number { return this.value; }
}
class Derived extends Base {
  static { this.value = super.read() + 1; }
}
class WithStaticMethod extends Base {
  static { this.value = this.read() + 10; }
}
console.log(Config.total, log.join("|"));
console.log(Derived.value, WithStaticMethod.value, Base.value);
console.log(Config.make() instanceof Config);`,
  );

  diff(
    "static fields, static blocks and methods keep declaration order",
    `const log: string[] = [];
class Ordered {
  static first = (log.push("field1"), 1);
  static { log.push("block1:" + Ordered.first); }
  static second = (log.push("field2"), 2);
  static method(): string { log.push("method"); return "m"; }
  static { log.push("block2:" + Ordered.method()); }
}
console.log(log.join(","), Ordered.first, Ordered.second);`,
  );

  diff(
    "`super[key]` and static accessors",
    `const KEY = "value";
class Base {
  static value = 1;
  static get computed(): number { return Base.value * 2; }
  static set computed(v: number) { Base.value = v; }
  static total = 0;
  static set add(v: number) { this.total += v; }
  name(): string { return "base"; }
  scaled(by: number): number { return by * 2; }
}
class Derived extends Base {
  static { this.value = 10; }
  name(): string { return super["name"]() + "+derived"; }
  scaled(by: number): number { return super.scaled(super["scaled"](by)); }
}
console.log(Derived.value, Base.computed, Derived.computed);
Derived.computed = 6;
Derived.add = 4;
console.log(Base.value, Derived.total, Base.total);
console.log(new Derived().name(), new Derived().scaled(3));
const pick = "computed";
console.log(Base[pick], Derived[pick]);
`,
  );
});
