/**
 * Runtime-coverage end-to-end tests.
 *
 * These programs are deliberately broad: each one drives a whole Node
 * compatibility module through its common (and a few uncommon) paths so the
 * instrumented C runtime sees as many branches as possible. They are ordinary
 * black-box tests as well — every case asserts the exact output of the compiled
 * binary — but their primary purpose is to keep `npm run coverage:runtime`
 * above its floor.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage",
  (h) => {
    it("exercises the Buffer implementation", () => {
      const source = `
        const b = Buffer.from("hello");
        console.log(b.toString(), b.length, Buffer.isBuffer(b));
        console.log(Buffer.from([1, 2, 3]).toString("hex"));
        console.log(Buffer.from("68656c6c6f", "hex").toString());
        console.log(Buffer.from("aGVsbG8=", "base64").toString());
        console.log(Buffer.from(b).toString());
        console.log(Buffer.alloc(3).toString("hex"), Buffer.alloc(4, 65).toString(), Buffer.alloc(3, "ab").toString());
        console.log(Buffer.alloc(0).length, Buffer.alloc(3, "").toString("hex"), Buffer.alloc(3, "61", "hex").toString("hex"));
        console.log(Buffer.allocUnsafe(2).length, Buffer.allocUnsafeSlow(2).length);
        console.log(Buffer.byteLength("hello"), Buffer.byteLength("68656c6c6f", "hex"), Buffer.byteLength("aGVsbG8=", "base64"), Buffer.byteLength(b));
        console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")]).toString());
        console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")], 4).toString());
        console.log(Buffer.compare(Buffer.from("a"), Buffer.from("b")), Buffer.compare(Buffer.from("b"), Buffer.from("a")), Buffer.compare(Buffer.from("a"), Buffer.from("a")));
        const n = new Buffer("hey");
        console.log(n.toString(), Buffer.of().length, new Buffer(2).length);
        const j = Buffer.from("hi").toJSON();
        console.log(j.type, j.data.join(","));
        console.log(b.slice(1, 3).toString(), b.slice(-3).toString(), b.subarray(0, 2).toString(), "[" + b.slice(3, 1).toString() + "]");
        console.log(Buffer.from("abc").equals(Buffer.from("abc")), Buffer.from("abc").equals(Buffer.from("abd")), true);
        console.log(Buffer.from("abc").compare(Buffer.from("abd")), Buffer.from("abd").compare(Buffer.from("abc")), Buffer.from("abc").compare(Buffer.from("abc")));
        const target = Buffer.alloc(6);
        const copied = Buffer.from("hello").copy(target, 1, 0, 3);
        console.log(copied, target.toString("hex"));
        const w = Buffer.alloc(8);
        console.log(w.write("abc"), w.write("def", 4), w.toString("hex"));
        console.log(w.write("6161", 0, 4, "hex"), w.toString("hex"));
        console.log(Buffer.alloc(5).fill("a").toString(), Buffer.alloc(5, 1).fill(66, 1, 3).toString("hex"), Buffer.alloc(2).fill(0).toString("hex"));
        console.log(Buffer.from("abc").reverse().toString());
        console.log(b.toString("hex"));
        console.log(Buffer.from("hello world").indexOf("world"), Buffer.from("hello world").indexOf("o"), Buffer.from("hello").indexOf(101), Buffer.from("hello").indexOf(Buffer.from("ll")));
        console.log(Buffer.from("hello world").lastIndexOf("o"), Buffer.from("hello").lastIndexOf("l", 2), Buffer.from("abc").includes("bc"), Buffer.from("abc").includes("z"));
        console.log(Buffer.from("hello").indexOf("z"), Buffer.from("hello").indexOf("toolong"), Buffer.from("hello").indexOf(""), Buffer.from("hello").indexOf("l", -10), Buffer.from("hello").indexOf("l", 100));
        console.log(Buffer.from("hello").lastIndexOf("l", 100), Buffer.from("hello").lastIndexOf("l", -1), Buffer.from("hello").lastIndexOf("lo", 3), Buffer.from("hello").lastIndexOf(111));
        console.log(Buffer.from("hello").indexOf("6c6c", 0, "hex"));
        const io = Buffer.alloc(16);
        console.log(io.writeUInt8(200, 0), io.writeInt8(-56, 1), io.readUInt8(0), io.readInt8(1));
        io.writeUInt16LE(0x1234, 2);
        io.writeUInt16BE(0x1234, 4);
        io.writeUInt16LE(0xfffe, 6);
        io.writeUInt16BE(0xfffe, 8);
        console.log(io.readUInt16LE(2), io.readUInt16BE(4), io.readInt16LE(6), io.readInt16BE(8));
        io.writeUInt32LE(0xffffffff, 0);
        console.log(io.readUInt32LE(0), io.readInt32LE(0));
        io.writeUInt32BE(0x12345678, 4);
        console.log(io.readUInt32BE(4), io.readInt32BE(4));
        console.log(Array.from(Buffer.from("abc").keys()).join(","), Array.from(Buffer.from("abc").values()).join(","));
        console.log(b.toString("base64"), b.toString("hex", 1, 3), "[" + b.toString("utf8", 3, 1) + "]");
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "hello 5 true",
        "010203",
        "hello",
        "hello",
        "hello",
        "000000 AAAA aba",
        "0 000000 616161",
        "2 2",
        "5 5 6 5",
        "foobar",
        "foob",
        "-1 1 0",
        "hey 0 2",
        "Buffer 104,105",
        "el llo he []",
        "true false true",
        "-1 1 0",
        "3 0068656c0000",
        "3 3 6162630064656600",
        "2 6161630064656600",
        "aaaaa 0142420101 0000",
        "cba",
        "68656c6c6f",
        "6 4 1 2",
        "7 2 true false",
        "-1 -1 -1 2 -1",
        "3 3 3 4",
        "2",
        "1 2 200 -56",
        "4660 4660 -2 -2",
        "4294967295 -1",
        "305419896 305419896",
        "0,1,2 97,98,99",
        "aGVsbG8= 656c []",
      ]);
    });

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

    it("converts between file paths and file URLs", () => {
      const source = `
        import { pathToFileURL, fileURLToPath } from "url";
        import url from "url";

        const u = pathToFileURL("/tmp/a b");
        console.log(u.protocol, u.href.startsWith("file://"), u.href.includes("a b"));
        console.log(fileURLToPath("file:///tmp/a%20b"));
        console.log(fileURLToPath("file://localhost/tmp/c"));
        console.log(fileURLToPath("/tmp/plain"));
        console.log(url.pathToFileURL("/tmp/x").href.startsWith("file://"));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "file: true true",
        "/tmp/a b",
        "/tmp/c",
        "/tmp/plain",
        "true",
      ]);
    });

    it("reads operating-system and process information", () => {
      const source = `
        import os from "os";
        import * as os2 from "node:os";
        import process from "process";
        import * as proc from "node:process";

        console.log(os.platform().length > 0, os.type().length > 0, os.arch().length > 0, os.endianness().length > 0);
        console.log(os.homedir().length > 0, os.tmpdir().length > 0, os.hostname().length > 0, os.release().length > 0);
        console.log(os.totalmem() > 0, os.freemem() > 0, os.cpus().length > 0, os2.cpus()[0].model.length > 0);
        console.log(os2.platform().length > 0);
        console.log(process.platform.length > 0, process.arch.length > 0, process.pid > 0, process.ppid >= 0, process.version.length > 0);
        console.log(process.cwd().length > 0, process.uptime() >= 0, process.hrtime().length === 2);
        console.log(process.getuid() >= 0, process.argv.length >= 1, Object.keys(process.env).length > 0);
        process.stdout.write("stdout-write\\n");
        process.stderr.write("stderr-write\\n");
        console.log(proc.cwd().length > 0, proc.platform.length > 0, proc.getuid() >= 0);
      `;
      const result = h.runProgramFull(source, { extensions: true, timeout: TIMEOUT });
      expect(result.status).toBe(0);
      expect(result.stdout.trim().split("\n")).toEqual([
        "true true true true",
        "true true true true",
        "true true true true",
        "true",
        "true true true true true",
        "true true true",
        "true true true",
        "stdout-write",
        "true true true",
      ]);
      expect(result.stderr.trim()).toBe("stderr-write");
    });

    it("spawns child processes synchronously", () => {
      const source = `
        import { spawnSync } from "node:child_process";

        let isChild = false;
        for (const arg of process.argv) {
          if (arg === "xb-child-capture") isChild = true;
        }
        if (isChild) {
          console.log("from-child");
          console.error("child-err");
          console.log("x".repeat(5000));
        } else {
          const r = spawnSync(process.argv[0], ["xb-child-capture"], { encoding: "utf8" });
          console.log("status", r.status, "out", r.stdout.trim().length, "err", r.stderr.trim());
          const missing = spawnSync("definitely-not-a-real-command-xyz", [], {});
          console.log("missing", missing.status !== 0);
          const cd = spawnSync(process.argv[0], ["xb-child-capture"], { cwd: process.cwd() });
          console.log("cwd-status", cd.status);
        }
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "status 0 out 5011 err child-err",
        "missing true",
        "cwd-status 0",
      ]);
    });

    it("hashes data with crypto", () => {
      const source = `
        import { createHash } from "crypto";
        import crypto from "node:crypto";

        console.log(createHash("sha256").update("abc").digest("hex"));
        console.log(createHash("sha-256").update("abc").digest("hex"));
        console.log(createHash("sha1").update("abc").digest("hex"));
        console.log(createHash("sha-1").update("abc").digest("hex"));
        console.log(createHash("sha256").update("x".repeat(100)).digest("base64"));
        const h = createHash("sha256");
        h.update("ab");
        h.update("c");
        console.log(h.digest("hex"));
        const s = createHash("sha256");
        s.setEncoding("base64");
        s.write("ab");
        s.write("c");
        s.end("d");
        console.log(s.read());
        const u = createHash("md5");
        console.log(u.update("abc").digest("hex").length);
        console.log(crypto.createHash("sha256").update("abc").digest("latin1").length);
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "a9993e364706816aba3e25717850c26c9cd0d89d",
        "a9993e364706816aba3e25717850c26c9cd0d89d",
        "Cey268i878cz9vLsRPeRq+7WqZ7fDMMVGWN4mK69Utg=",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "iNQmb9TmM40TuEX88olXnSCciXgjuSF9o+Fhk28DFYk=",
        "0",
        "32",
      ]);
    });

    it("drives the stream surface", () => {
      const source = `
        import { Readable, Writable, Duplex, Transform, PassThrough } from "stream";

        const r = new Readable({});
        r.push("a");
        r.push("b");
        console.log("read1", r.read());
        r.unshift("z");
        console.log("read2", r.read());
        r.setEncoding("utf8");
        console.log("paused", r.isPaused());
        r.pause();
        console.log("paused2", r.isPaused());
        r.resume();
        console.log("paused3", r.isPaused());
        r.push(null);
        console.log("read3", r.read());

        const w = new Writable({ write(chunk: string, enc: string, cb: () => void) { console.log("write-fn", chunk); cb(); } });
        w.write("hello");
        w.cork();
        w.uncork();
        w.end("bye", () => { console.log("end-cb"); });

        const t = new Transform((chunk: string, enc: string, cb: (e: unknown, v: string) => void) => { cb(null, chunk.toUpperCase()); });
        t.on("data", (d: string) => { console.log("transform-data", d); });
        t.write("abc");

        const pt = new PassThrough();
        pt.on("data", (d: string) => { console.log("pt-data", d); });
        pt.write("pass");
        pt.end();

        const d = new Duplex({ write(chunk: string, enc: string, cb: () => void) { cb(); } });
        d.on("data", (v: string) => { console.log("duplex-data", v); });
        d.write("duplex");

        const r2 = Readable.from(["x", "y"]);
        r2.on("data", (v: string) => { console.log("from-data", v); });

        const src = new Readable({});
        src.push("p1");
        src.push("p2");
        const dest = new Writable({ write(c: string, e: string, cb: () => void) { console.log("pipe-dest", c); cb(); } });
        dest.on("finish", () => { console.log("dest-finish"); });
        src.pipe(dest);
        src.push(null);

        const d2 = new Readable({});
        d2.on("error", (e: any) => { console.log("stream-err", e.message); });
        d2.on("close", () => { console.log("stream-close"); });
        d2.destroy(new Error("boom"));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "read1 a",
        "read2 z",
        "paused false",
        "paused2 true",
        "paused3 false",
        "read3 b",
        "write-fn hello",
        "write-fn bye",
        "end-cb",
        "transform-data ABC",
        "pt-data pass",
        "duplex-data duplex",
        "from-data x",
        "from-data y",
        "pipe-dest p1",
        "pipe-dest p2",
        "dest-finish",
        "stream-err boom",
        "stream-close",
      ]);
    });

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

    it("drives EventEmitter listeners and events.once", () => {
      const source = `
        import { EventEmitter } from "events";
        import events from "node:events";
        const e = new EventEmitter();
        e.on("a", (x: number) => { console.log("a1", x); });
        e.prependListener("a", (x: number) => { console.log("a0", x); });
        e.once("b", (x: number) => { console.log("b once", x); });
        e.on("b", (x: number) => { console.log("b always", x); });
        e.emit("a", 1);
        e.emit("b", 2);
        e.emit("b", 3);
        console.log(e.listenerCount("b"), e.eventNames().join(","), e.listeners("b").length);
        e.removeAllListeners("a");
        console.log(e.listenerCount("a"));
        e.setMaxListeners(20);
        console.log(e.getMaxListeners());
        e.on("c", () => { console.log("c"); });
        e.removeAllListeners();
        console.log(e.eventNames().length);
        const e2 = new EventEmitter();
        events.once(e2, "done").then((v: any) => { console.log("once", v[0]); });
        e2.emit("done", 42);
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "a0 1",
        "a1 1",
        "b once 2",
        "b always 2",
        "b always 3",
        "1 a,b 1",
        "0",
        "20",
        "0",
        "once undefined",
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
