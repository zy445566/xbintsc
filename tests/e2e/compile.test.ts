/**
 * End-to-end tests: compile TypeScript all the way to a native binary and run
 * it. Each program is compiled and executed through the shared
 * {@link describeE2E} harness, so the suite is skipped when no
 * clang-compatible compiler is available. Node extension coverage lives in
 * `compile-node.test.ts`.
 */

import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation", (harness) => {
  const { runProgram, runProgramFull } = harness;

  it("evaluates arithmetic and prints results", () => {
    expect(runProgram('console.log("sum", 2 + 3 * 4);')).toBe("sum 14");
  });

  it("supports arbitrary-precision BigInt values", () => {
    const source = `
      const a = 123456789012345678901234567890n;
      console.log(a * a);
      console.log((2n ** 100n).toString(16));
      console.log(-7n / 3n, -7n % 3n);
      console.log(~5n, 1n << 130n);
      console.log(typeof 1n, BigInt.asIntN(8, 0xFFn), BigInt.asUintN(8, 0xFFn));
      console.log("x" + 5n, 2n < 3n, 2n === 2);
    `;
    expect(runProgram(source)).toBe(
      [
        "15241578753238836750495351562536198787501905199875019052100n",
        "10000000000000000000000000",
        "-2n -1n",
        "-6n 1361129467683753853853498429727072845824n",
        "bigint -1n 255n",
        "x5 true false",
      ].join("\n"),
    );
  });

  it("interpolates template literals", () => {
    const source = `
      const name = "world";
      console.log(\`Hello, \${name}!\`);
      console.log(\`\${1 + 1}-\${2 + 2}\`, \`plain\`, "[" + \`\` + "]");
    `;
    expect(runProgram(source)).toBe("Hello, world!\n2-4 plain []");
  });

  it("runs recursive functions", () => {
    const source = `
      function fib(n: number): number {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
      }
      console.log(fib(10));
    `;
    expect(runProgram(source)).toBe("55");
  });

  it("supports loops, arrays and string concatenation", () => {
    const source = `
      const numbers = [1, 2, 3, 4];
      let total = 0;
      for (const n of numbers) {
        total += n;
      }
      console.log("total = " + total);
    `;
    expect(runProgram(source)).toBe("total = 10");
  });

  it("implements closures with by-reference captures", () => {
    const source = `
      const makeCounter = () => {
        let count = 0;
        return () => {
          count += 1;
          return count;
        };
      };
      const next = makeCounter();
      console.log(next(), next(), next());
    `;
    expect(runProgram(source)).toBe("1 2 3");
  });

  it("prints objects and arrays with JS-like formatting", () => {
    const source = `
      const user = { name: "ada", tags: ["math", "code"], age: 36 };
      console.log(user);
    `;
    expect(runProgram(source)).toBe("{ name: 'ada', tags: [ 'math', 'code' ], age: 36 }");
  });

  it("orders object keys like JavaScript", () => {
    const source = `
      console.log(JSON.stringify({ b: 1, a: 2, 10: 3, 2: 4 }));
      const o: any = {};
      o.z = 1; o[5] = 2; o.a = 3; o[1] = 4; o["01"] = 5; o[100] = 6;
      console.log(Object.keys(o).join(" "));
      let seen = "";
      for (const k in o) seen += k + ",";
      console.log(seen);
      console.log(JSON.stringify(Object.fromEntries([["b", 1], ["2", 2], ["a", 3]])));
    `;
    expect(runProgram(source)).toBe(
      [
        '{"2":4,"10":3,"b":1,"a":2}',
        "1 5 100 z a 01",
        "1,5,100,z,a,01,",
        '{"2":2,"b":1,"a":3}',
      ].join("\n"),
    );
  });

  it("supports switch statements with fall-through", () => {
    const source = `
      function size(n: number): string {
        switch (n) {
          case 0:
            return "none";
          case 1:
          case 2:
            return "few";
          default:
            return "many";
        }
      }
      console.log(size(0), size(1), size(2), size(7));
    `;
    expect(runProgram(source)).toBe("none few few many");
  });

  it("provides common array and string methods", () => {
    const source = `
      const xs = [3, 1, 2];
      xs.push(4);
      const doubled = xs.map((x) => x * 2).filter((x) => x > 4);
      const total = xs.reduce((a, x) => a + x, 0);
      console.log(xs.join("-"), doubled.join(","), total, xs.includes(2), xs.indexOf(1));
      const s = "  Hello World  ";
      console.log(s.trim().toUpperCase(), s.trim().slice(0, 5), "a,b,c".split(",").join("|"));
    `;
    expect(runProgram(source)).toBe("3-1-2-4 6,8 10 true 1\nHELLO WORLD Hello a|b|c");
  });

  it("supports Math, global functions and console levels", () => {
    const source = `
      console.log(Math.max(1, 9, 4), Math.abs(-3), Math.floor(2.9), Math.pow(2, 8), Math.PI > 3.14);
      console.log(parseInt("42px"), parseFloat("2.5x"), isNaN(NaN), isFinite(1), Number("7"), String(9), Boolean(0));
      console.warn("warned");
      console.error("failed");
    `;
    const result = runProgramFull(source);
    expect(result.status).toBe(0);
    expect(result.stdout.trim()).toBe("9 3 2 256 true\n42 2.5 true true 7 9 false");
    expect(result.stderr.trim()).toBe("warned\nfailed");
  });

  it("parses large integer literals without 32-bit clamping", () => {
    // Regression: `parseInt` used to go through `strtol`, whose `long` is only
    // 32 bits on Windows, so hex literals above INT32_MAX (used by the
    // compiler's own `hashText`) were clamped to 2147483647.
    const source = `
      console.log(parseInt("cbf29ce4", 16), parseInt("80000000", 16), parseInt("100000000", 16));
      console.log(parseInt("0xcbf29ce4"), parseInt("0x80000000"), parseInt("4503599627370496"));
      console.log(parseInt("-42"), parseInt("0x10", 10), parseInt("42px"));
      console.log(isNaN(parseInt("xyz")));
    `;
    expect(runProgram(source)).toBe(
      "3421674724 2147483648 4294967296\n3421674724 2147483648 4503599627370496\n-42 0 42\ntrue",
    );
  });

  it("supports default parameters, rest parameters and arguments", () => {
    const source = `
      function greet(name: string = "world", mark: string = "!"): string {
        return "hi " + name + mark;
      }
      function total(...nums: number[]): number {
        let sum = 0;
        for (const n of nums) sum += n;
        return sum;
      }
      function countArgs(): number {
        return arguments.length;
      }
      console.log(greet(), greet("ada"), total(1, 2, 3), countArgs(1, 2, 3, 4));
    `;
    expect(runProgram(source)).toBe("hi world! hi ada! 6 4");
  });

  it("supports Object helpers, spread, in and delete", () => {
    const source = `
      const base = { a: 1, b: 2 };
      const merged = Object.assign({}, base, { c: 3 });
      const copy = { ...base, a: 9 };
      console.log(Object.keys(base).join(","), Object.values(base).join(","));
      console.log(Object.entries(base).map((e) => e[0] + "=" + e[1]).join(","));
      console.log(merged.a, merged.b, merged.c, copy.a, "a" in base, "z" in base);
      delete base.a;
      console.log("a" in base, Object.keys(base).join(","));
    `;
    expect(runProgram(source)).toBe("a,b 1,2\na=1,b=2\n1 2 3 9 true false\nfalse b");
  });

  it("iterates object keys with for...in", () => {
    const source = `
      const obj = { x: 1, y: 2 };
      let out = "";
      for (const key in obj) out += key + "=" + obj[key] + ";";
      console.log(out);
    `;
    expect(runProgram(source)).toBe("x=1;y=2;");
  });

  it("supports try/catch/finally including returns and loop breaks", () => {
    const source = `
      function safe(n: number): number {
        try {
          if (n < 0) throw "negative";
          return n * 2;
        } catch (err) {
          return -1;
        }
      }
      function cleanup(): string {
        let log = "";
        try {
          log += "try;";
          throw "boom";
        } catch (e) {
          log += "catch:" + e + ";";
        } finally {
          log += "finally";
        }
        return log;
      }
      console.log(safe(5), safe(-2), cleanup());
    `;
    expect(runProgram(source)).toBe("10 -1 try;catch:boom;finally");
  });

  it("runs finally blocks on early return, break and continue", () => {
    const source = `
      const events: string[] = [];
      function f(n: number): string {
        try {
          events.push("try" + n);
          if (n === 1) return "ret1";
          if (n === 2) throw "boom" + n;
        } catch (e) {
          events.push("catch" + e);
          if (n === 3) return "ret3";
        } finally {
          events.push("finally" + n);
        }
        return "done" + n;
      }
      console.log(f(0), f(1), f(2), f(3));
      console.log(events.join("|"));
      const loop: string[] = [];
      for (let i = 0; i < 5; i++) {
        try {
          if (i === 2) break;
          if (i === 4) continue;
          loop.push("i" + i);
        } finally {
          loop.push("F" + i);
        }
      }
      console.log(loop.join("|"));
      function nested(): string {
        try {
          try {
            return "inner";
          } finally {
            events.push("innerFinally");
          }
        } finally {
          events.push("outerFinally");
        }
      }
      console.log(nested(), events[events.length - 2], events[events.length - 1]);
    `;
    expect(runProgram(source)).toBe(
      [
        "done0 ret1 done2 done3",
        "try0|finally0|try1|finally1|try2|catchboom2|finally2|try3|finally3",
        "i0|F0|i1|F1|F2",
        "inner innerFinally outerFinally",
      ].join("\n"),
    );
  });

  it("implements abstract equality with ToPrimitive", () => {
    const source = `
      console.log(([] as any) == 0, ([] as any) == "", ([1] as any) == 1, ([1, 2] as any) == "1,2");
      console.log(({} as any) == "[object Object]");
      console.log(([1] as any) == [1], ([] as any) == false, ([] as any) == null);
      console.log(0 == "0", 0 == false, "" == false, null == undefined, null == 0, undefined == 0);
      console.log(NaN == NaN, (1 as any) == "1");
    `;
    expect(runProgram(source)).toBe(
      [
        "true true true true",
        "true",
        "false true false",
        "true true true true false false",
        "false true",
      ].join("\n"),
    );
  });

  it("supports labeled break and continue across nested loops", () => {
    const source = `
      const out: string[] = [];
      outer: for (let i = 0; i < 3; i++) {
        inner: for (let j = 0; j < 3; j++) {
          if (j === 1) continue outer;
          if (i === 2) break outer;
          out.push(i + "," + j);
        }
      }
      block: {
        out.push("before");
        if (out.length > 0) break block;
        out.push("unreachable");
      }
      out.push("after");
      function f(): number {
        let total = 0;
        loop: for (let i = 0; i < 5; i++) {
          try {
            if (i === 2) continue loop;
            if (i === 4) break loop;
            total += i;
          } finally {
            total += 100;
          }
        }
        return total;
      }
      console.log(out.join("|"), f());
    `;
    expect(runProgram(source)).toBe("0,0|1,0|before|after 504");
  });

  it("supports #private fields, methods and static members", () => {
    const source = `
      class Counter {
        #count = 0;
        static #instances = 0;
        constructor() { Counter.#bumpStatic(); }
        #bump(by: number): void { this.#count += by; }
        static #bumpStatic(): void { Counter.#instances++; }
        inc(by: number): number { this.#bump(by); return this.#count; }
        get count(): number { return this.#count; }
        set count(v: number) { this.#count = v; }
        static instances(): number { return Counter.#instances; }
      }
      class Sub extends Counter {
        #label = "sub";
        constructor() { super(); this.inc(5); }
        describe(): string { return this.#label + ":" + this.count; }
      }
      const a = new Counter();
      const b = new Counter();
      a.inc(3);
      a.count = 10;
      console.log(a.count, b.count, Counter.instances());
      console.log(new Sub().describe());
    `;
    expect(runProgram(source)).toBe("10 0 2\nsub:5");
  });

  it("encodes and decodes URIs like the specification", () => {
    const source = `
      console.log(encodeURI("a/b?c=d&e#f"), encodeURIComponent("a/b?c=d&e#f"));
      console.log(encodeURI("café"), encodeURIComponent("😀 中文"));
      console.log(decodeURI("%2f%2F"), decodeURIComponent("a%23b"));
      console.log(decodeURIComponent("%C3%A9"), decodeURIComponent("%F0%9F%98%80"));
      console.log(typeof encodeURIComponent, typeof decodeURI);
      try { decodeURIComponent("%E0%80%80"); } catch (e) { console.log(String(e)); }
    `;
    expect(runProgram(source)).toBe(
      [
        "a/b?c=d&e#f a%2Fb%3Fc%3Dd%26e%23f",
        "caf%C3%A9 %F0%9F%98%80%20%E4%B8%AD%E6%96%87",
        "%2f%2F a#b",
        "é 😀",
        "function function",
        "URIError: URI malformed",
      ].join("\n"),
    );
  });

  it("exposes RegExp capture groups in exec, match, replace and split", () => {
    const source = `
      const m = "12-34".match(/(\\d+)-(\\d+)/)!;
      console.log(JSON.stringify([m[0], m[1], m[2], m.index]), Array.isArray(m), m.length);
      console.log(JSON.stringify("a1b2c3".split(/(\\d)/)));
      console.log(JSON.stringify("a1b2c3".split(/\\d/)));
      console.log("2020-01-02".replace(/(\\d+)-(\\d+)-(\\d+)/, "$3/$2/$1"));
      console.log("a1b2".replace(/(\\d)/g, "[$1]"));
      console.log("a1b2".replace(/\\d/g, (x: string) => "<" + x + ">"));
      console.log("a.b.c".search(/\\./));
      console.log(JSON.stringify("abc".split(/(?:)/)));
    `;
    expect(runProgram(source)).toBe(
      [
        '["12-34","12","34",0] true 3',
        '["a","1","b","2","c","3",""]',
        '["a","b","c",""]',
        "02/01/2020",
        "a[1]b[2]",
        "a<1>b<2>",
        "1",
        '["a","b","c"]',
      ].join("\n"),
    );
  });

  it("supports the immutable array methods and Object statics", () => {
    const source = `
      const a = [3, 1, 2];
      console.log(JSON.stringify(a.toSorted((x, y) => x - y)), JSON.stringify(a));
      console.log(JSON.stringify(a.toReversed()), JSON.stringify(a));
      console.log(JSON.stringify(a.toSpliced(1, 1, 9, 9)), JSON.stringify(a));
      console.log(JSON.stringify(a.with(1, 9)), JSON.stringify(a.with(-1, 9)));
      console.log(JSON.stringify(Object.getOwnPropertyNames({ b: 1, a: 2 })));
      console.log(JSON.stringify(Object.getOwnPropertyNames([1, 2])));
      console.log(JSON.stringify(Object.groupBy([1, 2, 3, 4], (n: number) => (n % 2 === 0 ? "even" : "odd"))));
    `;
    expect(runProgram(source)).toBe(
      [
        "[1,2,3] [3,1,2]",
        "[2,1,3] [3,1,2]",
        "[3,9,9,2] [3,1,2]",
        "[3,9,2] [3,1,9]",
        '["b","a"]',
        '["0","1","length"]',
        '{"odd":[1,3],"even":[2,4]}',
      ].join("\n"),
    );
  });

  it("supports optional chaining", () => {
    const source = `
      const obj = { a: { b: 5 }, m: (x: number) => x + 1 };
      const empty: any = null;
      console.log(obj?.a?.b, empty?.a?.b, obj?.m?.(10), empty?.m?.(10));
      const arr = [1, 2, 3];
      console.log(arr?.length, arr?.map((x) => x * 2).join(","), empty?.length);
    `;
    expect(runProgram(source)).toBe("5 undefined 11 undefined\n3 2,4,6 undefined");
  });

  it("short-circuits a whole optional chain but throws past the guard", () => {
    const source = `
      const empty: any = null;
      const nested: any = { a: { b: null } };
      // A \`?.\` short-circuits every later member, so nothing past it is evaluated.
      console.log("A", empty?.a.b.c);
      console.log("B", (nested?.a)?.b);
      console.log("C", nested.a?.b?.c);
      // Once the chain has started, a non-optional member on null throws.
      try {
        console.log(nested?.b.c);
        console.log("D-ok");
      } catch (e) {
        console.log("D-threw");
      }
      try {
        console.log(nested.a?.b.c);
        console.log("E-ok");
      } catch (e) {
        console.log("E-threw");
      }
    `;
    expect(runProgram(source)).toBe("A undefined\nB null\nC undefined\nD-threw\nE-threw");
  });

  it("defaults Array.sort to string order and reveals circular references", () => {
    const source = `
      console.log([10, 1, 2, 20].sort().join(","));
      console.log(["banana", "apple", "cherry"].sort().join(","));
      console.log(JSON.stringify([3, undefined, 1, 2, undefined].sort()));
      console.log([5, 3, 8, 1].sort((a, b) => a - b).join(","));
      const o: any = { name: "x" };
      o.self = o;
      o.child = { parent: o };
      console.log(o);
      const arr: any = [1, 2];
      arr.push(arr);
      console.log(arr);
    `;
    expect(runProgram(source)).toBe(
      [
        "1,10,2,20",
        "apple,banana,cherry",
        "[1,2,3,null,null]",
        "1,3,5,8",
        "{ name: 'x', self: [Circular *1], child: { parent: [Circular *1] } }",
        "[ 1, 2, [Circular *1] ]",
      ].join("\n"),
    );
  });

  it("supports classes with new, this, statics, inheritance and instanceof", () => {
    const source = `
      class Animal {
        name: string;
        constructor(name: string) {
          this.name = name;
        }
        speak(): string {
          return this.name + " makes a sound";
        }
        static kind(): string {
          return "animal";
        }
      }
      class Dog extends Animal {
        constructor(name: string) {
          super(name);
        }
        speak(): string {
          return super.speak() + " (woof)";
        }
      }
      const dog = new Dog("Rex");
      console.log(dog.speak());
      console.log(Animal.kind(), dog instanceof Dog, dog instanceof Animal);
    `;
    expect(runProgram(source)).toBe("Rex makes a sound (woof)\nanimal true true");
  });

  it("supports async/await and promises", () => {
    const source = `
      async function greet(name: string): Promise<string> {
        return "hello " + name;
      }
      async function main(): Promise<void> {
        const message = await greet("world");
        console.log(message);
        const all = await Promise.all([Promise.resolve(1), Promise.resolve(2)]);
        console.log(all[0] + all[1]);
        const settled = await Promise.allSettled([Promise.resolve(1), Promise.reject("no")]);
        console.log(settled[0].status, settled[1].status);
        try {
          await Promise.reject("boom");
        } catch (err) {
          console.log("caught", err);
        }
      }
      main();
      Promise.resolve(10).then((x) => console.log("then", x + 1));
    `;
    expect(runProgram(source)).toBe("hello world\n3\nfulfilled rejected\ncaught boom\nthen 11");
  });

  it("supports Map, Set, JSON and extended standard library methods", () => {
    const source = `
      const map = new Map<string, number>();
      map.set("a", 1);
      map.set("b", 2);
      const nums = new Set<number>();
      nums.add(3);
      nums.add(3);
      console.log(map.get("a"), map.size, nums.has(3), nums.size);
      const obj = { x: 1, y: [2, 3] };
      console.log(JSON.stringify(obj));
      console.log(JSON.parse('{"k":5}').k);
      console.log([3, 1, 2].sort().join(","), [1, 2, 3].find((n) => n > 1));
      console.log("abc".padStart(5, "*"), (255).toString(16), (3.14159).toFixed(2));
      console.log(Number.isInteger(3), Array.isArray([1]), Math.imul(2, 3));
    `;
    expect(runProgram(source)).toBe(
      "1 2 true 1\n{\"x\":1,\"y\":[2,3]}\n5\n1,2,3 2\n**abc ff 3.14\ntrue true 6",
    );
  });

  it("assigns array.length and spreads iterables (Map/Set/string)", () => {
    const source = `
      const a = [10, 20, 30];
      a.length = 1;
      console.log(a.length, a[0], a[1]);
      a.length = 3;
      console.log(a.length, a[1], a[2]);
      const set = new Set<string>();
      set.add("x");
      set.add("y");
      set.add("x");
      console.log([...set].length, [...set].join(","), ["a", ...set, "b"].join(","));
      const map = new Map<string, number>();
      map.set("k", 1);
      map.set("v", 2);
      console.log([...map].length, [...map].map((p) => p[0] + ":" + p[1]).join(","));
      console.log([..."abc"].length, [..."abc"].join("-"));
    `;
    expect(runProgram(source)).toBe(
      "1 10 undefined\n3 undefined undefined\n2 x,y a,x,y,b\n2 k:1,v:2\n3 a-b-c",
    );
  });

  it("iterates Map and Set with for...of", () => {
    const source = `
      const map = new Map<string, number>();
      map.set("a", 1);
      map.set("b", 2);
      let out = "";
      for (const [k, v] of map) out += k + "=" + v + ";";
      const set = new Set<number>();
      set.add(10);
      set.add(20);
      let sum = 0;
      for (const n of set) sum += n;
      console.log(out, sum);
    `;
    expect(runProgram(source)).toBe("a=1;b=2; 30");
  });

  it("supports import and export across modules", () => {
    writeFileSync(
      join(harness.workdir, "math_dep.ts"),
      `
      export const PI = 3.14;
      export function add(a: number, b: number): number {
        return a + b;
      }
      export default function greet(name: string): string {
        return "hi " + name;
      }
      `,
    );
    const source = `
      import greet, { add, PI } from "./math_dep";
      console.log(add(2, 3), PI);
      console.log(greet("ada"));
    `;
    expect(runProgram(source)).toBe("5 3.14\nhi ada");
  });

  it("resolves `.js` specifiers to their TypeScript sources", () => {
    writeFileSync(join(harness.workdir, "esm_dep.ts"), `export const value = 42;`);
    const source = `
      import { value } from "./esm_dep.js";
      console.log(value);
    `;
    expect(runProgram(source)).toBe("42");
  });

  it("supports destructuring bindings, enums, regexes and Error", () => {
    const source = `
      const [a, b = 7, ...rest] = [1, undefined, 3, 4];
      const { x, y: z = 9 } = { x: 5 };
      enum Color { Red, Green = 5, Blue }
      const re = /^-[0-9]+/;
      console.log(a, b, rest.join(","), x, z);
      console.log(Color.Red, Color.Green, Color.Blue, Color[5]);
      console.log(re.test("-42abc"), re.test("nope"));
      try { throw new Error("boom"); } catch (e) { console.log(e.name + ": " + e.message); }
    `;
    expect(runProgram(source)).toBe(
      "1 7 3,4 5 9\n0 5 6 Green\ntrue false\nError: boom",
    );
  });

  it("supports destructuring assignments", () => {
    const source = `
      let a = 1, b = 2;
      [a, b] = [b, a];
      console.log(a, b);

      let c = 0, d = 0, rest: number[] = [];
      [c, d = 5, ...rest] = [10, undefined, 30, 40];
      console.log(c, d, rest.join(","));

      let x = 0, y = 0;
      [[x], [y]] = [[100], [200]];
      let p = 0, q = 0, r = 0;
      ({ p, q = 9, p: r } = { q: 7, p: 8 });
      console.log(x, y, p, q, r);

      const target = { v: 0 } as any;
      const list = [0, 0];
      [target.v, list[0]] = [5, 6];
      console.log(target.v, list[0]);

      const pairs = [[1, 2], [3, 4]];
      let u = 0, v = 0;
      for ([u, v] of pairs) console.log(u, v);
      for (const [f, s] of pairs) console.log(f + s);
    `;
    expect(runProgram(source)).toBe(
      "2 1\n10 5 30,40\n100 200 8 7 8\n5 6\n1 2\n3 4\n3\n7",
    );
  });

  it("supports destructuring parameters", () => {
    const source = `
      const pairs = [["a", 1], ["b", 2]];
      const mapped = pairs.map(([name, value]) => [name, { value }]);
      console.log(mapped[0][0], mapped[1][0]);
      console.log(mapped[0][1].value, mapped[1][1].value);

      function sum([a, b]: [number, number]) { return a + b; }
      console.log(sum([3, 4]));

      function greet({ name, greeting = "Hello" }: { name: string; greeting?: string }) {
        return greeting + ", " + name;
      }
      console.log(greet({ name: "Ada" }), greet({ name: "Bob", greeting: "Hi" }));

      const withDefault = ([a, b] = [10, 20]) => a + b;
      console.log(withDefault(), withDefault([1, 2]));
    `;
    expect(runProgram(source)).toBe(
      "a b\n1 2\n7\nHello, Ada Hi, Bob\n30 3",
    );
  });

  it("supports Function.prototype.call, apply and bind", () => {
    const source = `
      function add(a: number, b: number): number { return a + b; }
      console.log(add.call(null, 1, 2), add.apply(null, [5, 6]));

      const obj = {
        base: 10,
        sum(a: number, b: number): number { return this.base + a + b; },
      };
      console.log(obj.sum.call(obj, 1, 2), obj.sum.apply(obj, [3, 4]));

      const bound = obj.sum.bind(obj, 1);
      console.log(bound(2));
      const bound2 = add.bind(null, 10);
      console.log(bound2(5));

      class Greeter {
        prefix = "hi";
        greet(name: string): string { return this.prefix + " " + name; }
      }
      const g = new Greeter();
      console.log(g.greet.call(g, "ada"), g.greet.bind(g)("cy"));
    `;
    expect(runProgram(source)).toBe(
      "3 11\n13 17\n13\n15\nhi ada hi cy",
    );
  });

  it("supports object-literal getters and setters", () => {
    const source = `
      const obj = {
        _x: 0,
        get x(): number { return this._x; },
        set x(v: number) { this._x = v * 2; },
        get double(): number { return this._x * 2; },
        method(a: number, b: number) { return a + b; },
      };
      obj.x = 5;
      console.log(obj.x, obj.double);
      console.log(obj.method(2, 3));
      console.log(Object.keys(obj).join(","));

      const base = 7;
      const o2 = { base, get next() { return base + 1; } };
      console.log(o2.base, o2.next);

      const o3 = { get ["dyn" + "amic"]() { return 42; } };
      console.log(o3.dynamic);
    `;
    expect(runProgram(source)).toBe(
      "10 20\n5\n_x,x,double,method\n7 8\n42",
    );
  });

  it("supports first-class built-in method values", () => {
    const source = `
      const arr = [1, 2, 3];
      console.log(typeof arr.map, arr.map.name, arr.map.length);
      console.log(arr.map?.((x: number) => x * 2).join(","));

      const text = "abc";
      console.log(typeof text.slice, text.slice(1), text.split?.("").join("-"));

      const obj = { base: 10, add(n: number) { return this.base + n; } };
      console.log(obj.add?.(5));

      const detached = arr.map;
      try {
        (detached as any)((x: number) => x);
        console.log("no throw");
      } catch (e) {
        console.log("threw");
      }
    `;
    expect(runProgram(source)).toBe(
      "function map 1\n2,4,6\nfunction bc a-b-c\n15\nthrew",
    );
  });
});
