/**
 * Runtime-coverage tests for `util` and `assert`.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: node-modules",
  (h) => {
    it("exercises util helpers and predicates", () => {
      const source = `
        import util, {
          format, formatWithOptions, inspect, isDeepStrictEqual, inherits, deprecate, promisify,
          isString, isNumber, isBoolean, isUndefined, isNull, isFunction, isArray, isObject,
          isBuffer, isDate, isRegExp, isPromise, isError,
        } from "util";

        console.log(format("%s %d %i %f %j %o %O %% %c", "s", 3.9, 4.2, 1.5, { a: 1 }, { b: 2 }, [1, 2], "css"));
        console.log(format("%s", { a: 1 }), "|", format("%d", "5"), "|", format("%f", "2.5"));
        console.log(format(1, "two", { three: 3 }));
        console.log(format("only %s"), "|", format("%s and %d", "a"));
        console.log(formatWithOptions({ colors: true }, "%s=%d", "x", 2));
        console.log(inspect(undefined), inspect(null), inspect(true), inspect(1), inspect(10n), inspect("str"));
        console.log(inspect([1, [2, [3, [4]]]]));
        console.log(inspect({ a: 1, nested: { b: [1, 2] } }));
        function f(): void {}
        console.log(inspect(f));
        console.log(inspect(new Date(0)));
        console.log(inspect(/ab+c/gi));
        console.log(isDeepStrictEqual({ a: [1, { b: 2 }] }, { a: [1, { b: 2 }] }), isDeepStrictEqual({ a: 1 }, { a: 2 }));
        console.log(isDeepStrictEqual([1, 2], [1, 2, 3]), isDeepStrictEqual(NaN, NaN));
        class Base {}
        class Derived extends Base {}
        const d = new Derived();
        console.log(d instanceof Base);
        const dep = deprecate(f, "old");
        console.log(dep === f);
        function addAsync(a: number, b: number, cb: (e: unknown, v: number) => void): void { cb(null, a + b); }
        function failAsync(cb: (e: unknown, v: unknown) => void): void { cb(new Error("boom"), null); }
        const add = promisify(addAsync);
        add(2, 3).then((v: number) => { console.log("add", v); });
        const failing = promisify(failAsync);
        failing().then(() => { console.log("no"); }, (e: any) => { console.log("err", e.message); });
        console.log(
          isString("x"), isNumber(1), isBoolean(true), isUndefined(undefined), isNull(null),
          isFunction(f), isArray([]), isObject({}), isBuffer(Buffer.from("x")), isDate(new Date()),
          isRegExp(/x/), isPromise(Promise.resolve(1)), isError(new Error("e")),
        );
        console.log(util.format("%s!", "ns"), util.isNumber(3));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "s 3 4 1.500000 {\"a\":1} { b: 2 } [ 1, 2 ] %  'css'",
        "{ a: 1 } | 5 | 2.500000",
        "1 'two' { three: 3 }",
        "only %s | a and %d",
        "x=2",
        "undefined null true 1 10n 'str'",
        "[ 1, [ 2, [Array] ] ]",
        "{ a: 1, nested: { b: [Array] } }",
        "[Function]",
        "[object Object]",
        "[object Object]",
        "true false",
        "false true",
        "true",
        "false",
        "true true true true true true true true true true true true true",
        "ns! true",
        "add 5",
        "err boom",
      ]);
    });

    it("exercises the assert module", () => {
      const source = `
        import assert, { strictEqual, deepStrictEqual, throws, doesNotThrow, ifError, match, doesNotMatch, notStrictEqual, ok, equal, notEqual, deepEqual, notDeepEqual, rejects, doesNotReject } from "node:assert";

        function check(label: string, fn: () => void): void {
          try {
            fn();
            console.log(label, "ok");
          } catch (e: any) {
            console.log(label, "threw", e.name);
          }
        }

        check("ok-pass", () => assert.ok(1));
        check("ok-fail", () => assert.ok(0));
        check("equal-pass", () => assert.equal(1, "1"));
        check("equal-fail", () => assert.equal(1, 2, "custom equal"));
        check("notEqual-pass", () => assert.notEqual(1, 2));
        check("notEqual-fail", () => assert.notEqual(1, "1"));
        check("strictEqual-pass", () => assert.strictEqual(1, 1));
        check("strictEqual-fail", () => assert.strictEqual(1, "1"));
        check("notStrictEqual-pass", () => assert.notStrictEqual(1, "1"));
        check("notStrictEqual-fail", () => assert.notStrictEqual(1, 1));
        check("deepEqual-pass", () => assert.deepEqual({ a: [1, 2] }, { a: [1, 2] }));
        check("deepEqual-fail", () => assert.deepEqual({ a: 1 }, { a: 2 }));
        check("notDeepEqual-pass", () => assert.notDeepEqual({ a: 1 }, { a: 2 }));
        check("notDeepEqual-fail", () => assert.notDeepEqual({ a: 1 }, { a: 1 }));
        check("deepStrictEqual-pass", () => assert.deepStrictEqual([1, { b: 2 }], [1, { b: 2 }]));
        check("deepStrictEqual-fail", () => assert.deepStrictEqual([1], [2]));
        check("notDeepStrictEqual-pass", () => assert.notDeepStrictEqual([1], [2]));
        check("throws-pass", () => assert.throws(() => { throw new Error("x"); }));
        check("throws-fail", () => assert.throws(() => {}));
        check("throws-nonfn", () => assert.throws(1 as any));
        check("throws-message", () => assert.throws(() => { throw new Error("x"); }, "boom"));
        check("doesNotThrow-pass", () => assert.doesNotThrow(() => {}));
        check("doesNotThrow-fail", () => assert.doesNotThrow(() => { throw new Error("y"); }));
        check("ifError-null", () => assert.ifError(null));
        check("ifError-undefined", () => assert.ifError(undefined));
        check("ifError-obj", () => assert.ifError(new Error("z")));
        check("ifError-value", () => assert.ifError("bad"));
        check("match-regexp", () => assert.match("hello", /ell/));
        check("match-string", () => assert.match("hello", "ell"));
        check("match-fail", () => assert.match("hello", /xyz/, "no match"));
        check("doesNotMatch-pass", () => assert.doesNotMatch("hello", /xyz/));
        check("doesNotMatch-fail", () => assert.doesNotMatch("hello", /ell/));
        check("fail-default", () => assert.fail());
        check("fail-custom", () => assert.fail("custom failure"));

        strictEqual(1, 1);
        deepStrictEqual({ x: 1 }, { x: 1 });
        throws(() => { throw new Error("n"); });
        doesNotThrow(() => {});
        ifError(null);
        match("abc", /b/);
        doesNotMatch("abc", /z/);
        notStrictEqual(1, 2);
        ok(true);
        equal(1, "1");
        notEqual(1, 2);
        deepEqual([1], [1]);
        notDeepEqual([1], [2]);
        rejects();
        doesNotReject();
        console.log("named-imports ok");
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "ok-pass ok",
        "ok-fail threw AssertionError",
        "equal-pass ok",
        "equal-fail threw AssertionError",
        "notEqual-pass ok",
        "notEqual-fail threw AssertionError",
        "strictEqual-pass ok",
        "strictEqual-fail threw AssertionError",
        "notStrictEqual-pass ok",
        "notStrictEqual-fail threw AssertionError",
        "deepEqual-pass ok",
        "deepEqual-fail threw AssertionError",
        "notDeepEqual-pass ok",
        "notDeepEqual-fail threw AssertionError",
        "deepStrictEqual-pass ok",
        "deepStrictEqual-fail threw AssertionError",
        "notDeepStrictEqual-pass ok",
        "throws-pass ok",
        "throws-fail threw AssertionError",
        "throws-nonfn threw AssertionError",
        "throws-message ok",
        "doesNotThrow-pass ok",
        "doesNotThrow-fail threw AssertionError",
        "ifError-null ok",
        "ifError-undefined ok",
        "ifError-obj threw Error",
        "ifError-value threw AssertionError",
        "match-regexp ok",
        "match-string ok",
        "match-fail threw AssertionError",
        "doesNotMatch-pass ok",
        "doesNotMatch-fail threw AssertionError",
        "fail-default threw AssertionError",
        "fail-custom threw AssertionError",
        "named-imports ok",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
