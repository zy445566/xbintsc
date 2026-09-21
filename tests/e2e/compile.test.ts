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
    expect(runProgram(source)).toBe("{ name: ada, tags: [ math, code ], age: 36 }");
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

  it("supports optional chaining", () => {
    const source = `
      const obj = { a: { b: 5 }, m: (x: number) => x + 1 };
      const empty = null;
      console.log(obj?.a?.b, empty?.a?.b, obj?.m?.(10), empty?.m?.(10));
      const arr = [1, 2, 3];
      console.log(arr?.length, arr?.map((x) => x * 2).join(","), empty?.length);
    `;
    expect(runProgram(source)).toBe("5 undefined 11 undefined\n3 2,4,6 undefined");
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
});
