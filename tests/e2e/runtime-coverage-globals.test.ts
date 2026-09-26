/**
 * Runtime-coverage tests for the global objects, console helpers and BigInt.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: globals",
  (h) => {
    it("covers Object, Map, Set and JSON globals", () => {
      const source = `
        console.log(Object.keys({a:1,b:2}).join(","), Object.values({a:1,b:2}).join(","), Object.entries({a:1,b:2}).map(e => e[0]+e[1]).join(","));
        console.log(Object.getOwnPropertyNames([1,2]).join(","), Object.getOwnPropertyNames("ab").join(","));
        console.log(Object.assign({a:1}, {b:2}, {a:3}).a, Object.assign({}, {b:2}).b);
        const fz = Object.freeze({x:1});
        console.log(Object.isFrozen(fz), Object.isFrozen({}));
        console.log(Object.hasOwn({x:1},"x"), Object.hasOwn({x:1},"y"));
        console.log(Object.is(NaN, NaN), Object.is(0, -0), Object.is(1,1), Object.is({},{}));
        const proto = { greet() { return "hi"; } };
        const child = Object.create(proto);
        console.log(child.greet(), Object.getPrototypeOf(child) === proto);
        const grouped = Object.groupBy([1,2,3,4], (n: number) => n % 2 === 0 ? "even" : "odd");
        console.log(grouped.odd.join(","), grouped.even.join(","));
        console.log(Object.fromEntries([["a",1],["b",2]]).a);
        console.log(JSON.stringify({a:1,b:[2,3],c:"x"}), JSON.parse('{"a":1,"b":[2,3]}').b[1]);
        const m = new Map<string, number>(); m.set("a",1); m.set("b",2); console.log(m.get("a"), m.size, m.has("b"), m.delete("a"), m.size);
        const s = new Set<number>(); s.add(1); s.add(2); s.add(1); console.log(s.size, s.has(2), s.delete(1), s.size);
        console.log([...m.keys()].join(","), [...s.values()].join(","));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "a,b 1,2 a1,b2",
        "0,1,length 0,1,length",
        "3 2",
        "true false",
        "true false",
        "true false true false",
        "hi true",
        "1,3 2,4",
        "1",
        '{"a":1,"b":[2,3],"c":"x"} 3',
        "1 2 true true 1",
        "2 true true 1",
        "b 2",
      ]);
    });

    it("uses the console surface, inspect and base64 helpers", () => {
      const source = `
        console.count("x");
        console.count("x");
        console.countReset("x");
        console.count("x");
        console.group("g");
        console.groupEnd();
        console.assert(true, "no");
        console.assert(false, "assert-msg");
        console.dir({ a: 1 });
        console.time("t");
        console.timeLog("t");
        console.timeEnd("t");
        console.table([{ a: 1 }, { a: 2 }]);
        console.warn("warn");
        console.error("err");
        console.info("info");
        console.log(btoa("hello"), atob("aGVsbG8="));
        const cyc: any = { a: 1 };
        cyc.self = cyc;
        console.log(cyc);
        console.log({ b: 2, a: 1 });
        console.log([1, "two", true, null, undefined, 3n]);
        console.log(import.meta.url.length > 0, typeof import.meta.dirname, typeof import.meta.filename);
      `;
      const stdout = h
        .runProgram(source, { extensions: true, timeout: TIMEOUT })
        .split("\n")
        .map((line) => (/^t: .*ms$/.test(line) ? "t: <time>" : line))
        .join("\n");
      expect(stdout.split("\n")).toEqual([
        "x: 1",
        "x: 2",
        "x: 1",
        "g",
        "{ a: 1 }",
        "t: <time>",
        "t: <time>",
        "[ { a: 1 }, { a: 2 } ]",
        "info",
        "aGVsbG8= hello",
        "{ a: 1, self: [Circular *1] }",
        "{ b: 2, a: 1 }",
        "[ 1, 'two', true, null, undefined, 3n ]",
        "true string string",
      ]);
    });

    it("computes with BigInt", () => {
      const source = `
        console.log(BigInt("123"), BigInt(456), BigInt(true), BigInt(false), BigInt(10n));
        console.log(10n + 20n, 10n - 3n, 2n * 3n, 10n / 3n, 10n % 3n, 2n ** 10n);
        console.log(-5n, +5n, 5n < 10n, 5n == 5, 5n === 5);
        console.log(255n & 15n, 255n | 15n, 255n ^ 15n, ~0n, 1n << 4n, 256n >> 4n);
        console.log(10n > 5n, 10n >= 10n, 10n < 11n, 10n <= 9n, 5n != 6n, 5n !== 6);
        console.log((123456789012345678901234567890n).toString());
        console.log(Number(10n), +10n, (10n).toString(16), (255n).toString(2), (10n).toString(36));
        try { BigInt(3.5); console.log("no"); } catch (e: any) { console.log("range threw"); }
        try { BigInt({} as any); console.log("no"); } catch (e: any) { console.log("type threw"); }
        console.log(5n ** 0n, 0n ** 5n, 2n ** 64n);
        console.log((0n).toString(16), (-255n).toString(16));
        console.log(BigInt(-12), (-12n).toString(), (-10n) / 3n, (-10n) % 3n);
        console.log(100000000000000000000n + 1n, 99999999999999999999n * 3n);
        console.log(18446744073709551616n > 18446744073709551615n, 18446744073709551615n < 18446744073709551616n, 18446744073709551616n === 18446744073709551616n);
        console.log(1n << 100n, 1n << 200n, 2n ** 100n, 2n ** 200n);
        console.log(-1n & 255n, -1n | 0n, -5n ^ 3n, ~255n, -256n >> 4n, -1n >> 100n);
        console.log(BigInt("0xff"), BigInt("0b101"), BigInt("0o17"), BigInt("  42  "));
        try { BigInt("abc"); console.log("no"); } catch (e: any) { console.log("syntax threw"); }
        console.log(BigInt("-0"), BigInt("+5"), (255n).toString(8), (255n).toString(16));
        console.log(0n == 0, 0n === 0, 1n < 1, 1n > 0.5);
        console.log((2n ** 64n) / 3n, (2n ** 64n) % 7n);
        console.log((-(2n ** 64n)) / 3n, (-(2n ** 64n)) % 7n);
        console.log((10n).valueOf(), (10n) + "");
        console.log(9007199254740993n, BigInt(9007199254740993));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "123n 456n 1n 0n 10n",
        "30n 7n 6n 3n 1n 1024n",
        "-5n 5n true true false",
        "15n 255n 240n -1n 16n 16n",
        "true true true false true true",
        "123456789012345678901234567890",
        "10 10n a 11111111 a",
        "range threw",
        "type threw",
        "1n 0n 18446744073709551616n",
        "0 -ff",
        "-12n -12 -3n -1n",
        "100000000000000000001n 299999999999999999997n",
        "true true true",
        "1267650600228229401496703205376n 1606938044258990275541962092341162602522202993782792835301376n 1267650600228229401496703205376n 1606938044258990275541962092341162602522202993782792835301376n",
        "255n -1n -8n -256n -16n -1n",
        "255n 5n 15n 42n",
        "syntax threw",
        "0n 5n 377 ff",
        "true false false true",
        "6148914691236517205n 2n",
        "-6148914691236517205n -2n",
        "10n 10",
        "9007199254740993n 9007199254740992n",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
