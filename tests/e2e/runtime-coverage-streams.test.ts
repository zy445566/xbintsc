/**
 * Runtime-coverage tests for `crypto`, streams and `events`.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: streams",
  (h) => {
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
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
