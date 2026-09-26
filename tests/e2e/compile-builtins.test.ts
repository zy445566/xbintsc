/**
 * End-to-end tests for the built-in standard library (BigInt, collections, async, ...).
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end built-in library", (harness) => {
  const { runProgram, runProgramFull } = harness;

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
});
