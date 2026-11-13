/**
 * End-to-end tests for the worker API surface shared by `node:worker_threads`
 * and the Web Worker globals.
 *
 * These cases cover the parts of the API that the happy-path suites do not
 * reach: the structured-clone codec's scalar/collection branches, the
 * EventEmitter and DOM event methods on a worker object and on `parentPort`,
 * `MessagePort` methods, and the errors raised when a worker is already gone.
 *
 * Workers in these tests return instead of being terminated wherever possible:
 * a killed process never writes its LLVM coverage profile, so a natural exit is
 * what lets the runtime coverage job see the worker half of the code.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("worker API", (harness) => {
  const { runProgram } = harness;

  it("clones scalars, collections and shared references", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("message", (m) => {
          const checks: boolean[] = [
            1 / m.negZero === -Infinity,
            m.nan !== m.nan,
            m.inf === Infinity,
            m.negInf === -Infinity,
            m.undef === undefined,
            m.nul === null,
            m.yes === true && m.no === false,
            m.big === 12345678901234567890n,
            m.text.length === 20,
            m.text.charCodeAt(1) === 34,
            m.text.charCodeAt(3) === 92,
            m.text.charCodeAt(5) === 9,
            m.text.charCodeAt(7) === 10,
            m.text.charCodeAt(9) === 13,
            m.text.charCodeAt(11) === 8,
            m.text.charCodeAt(13) === 12,
            m.text.charCodeAt(15) === 1,
            m.text.indexOf("中文😀") === 16,
            m.emptyArray.length === 0,
            Object.keys(m.emptyObject).length === 0,
            m.shared.left === m.shared.right,
            m.map.get("k") === 1 && m.map.get("deep").x[1] === 2 && m.map.size === 2,
            m.set.has(2) && m.set.size === 2,
            m.date.getTime() === 1234,
            m.reg.source === "a+b" && m.reg.flags === "gi",
            m.nested.arr[0].x === 1 && m.nested.map.get("n").has(3),
          ];
          let all = true;
          for (const check of checks) if (!check) all = false;
          console.log(all, m.text.length, m.big, m.shared.left === m.shared.right);
        });
      } else {
        const shared = { n: 1 };
        const text =
          "q" +
          String.fromCharCode(34) +
          "b" +
          String.fromCharCode(92) +
          "t" +
          String.fromCharCode(9) +
          "n" +
          String.fromCharCode(10) +
          "r" +
          String.fromCharCode(13) +
          "b" +
          String.fromCharCode(8) +
          "f" +
          String.fromCharCode(12) +
          "c" +
          String.fromCharCode(1) +
          "中文😀";
        parentPort.postMessage({
          negZero: -0,
          nan: NaN,
          inf: Infinity,
          negInf: -Infinity,
          undef: undefined,
          nul: null,
          yes: true,
          no: false,
          big: 12345678901234567890n,
          text,
          emptyArray: [],
          emptyObject: {},
          shared: { left: shared, right: shared },
          map: new Map([["k", 1], ["deep", { x: [1, 2] }]]),
          set: new Set([1, 2]),
          date: new Date(1234),
          reg: /a+b/gi,
          nested: { arr: [{ x: 1 }], map: new Map([["n", new Set([3])]]) },
        });
        parentPort.close();
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "true 20 12345678901234567890n true",
    );
  });

  it("clones array buffers and every numeric typed array", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("message", (m) => {
          console.log(
            m.buffer.byteLength === 5,
            m.emptyBuffer.byteLength === 0,
            m.emptyU8.length === 0,
            m.i8[0] === -1 && m.i8.length === 1,
            m.u8a[0] === 7 && m.u8b[1] === 8 && m.u8c[2] === 9,
            m.clamped[0] === 255,
            m.i16[1] === 300 && m.u16[0] === 65535,
            m.i32[0] === -3 && m.u32[0] === 4294967295,
            m.f32[1] === -2.25 && m.f64[0] === -2.5,
          );
        });
      } else {
        parentPort.postMessage({
          buffer: new Uint8Array([1, 2, 3, 4, 5]).buffer,
          emptyBuffer: new ArrayBuffer(0),
          emptyU8: new Uint8Array(0),
          i8: new Int8Array([-1]),
          u8a: new Uint8Array([7]),
          u8b: new Uint8Array([7, 8]),
          u8c: new Uint8Array([7, 8, 9]),
          clamped: new Uint8ClampedArray([255]),
          i16: new Int16Array([-2, 300]),
          u16: new Uint16Array([65535]),
          i32: new Int32Array([-3]),
          u32: new Uint32Array([4294967295]),
          f32: new Float32Array([1.5, -2.25]),
          f64: new Float64Array([-2.5, 3.5]),
        });
        parentPort.close();
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("true true true true true true true true true");
  });

  it("exposes the Worker emitter and DOM event surface", () => {
    const source = `
      import { Worker, isMainThread, markAsUncloneable } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        const seen: string[] = [];
        const onTick = () => { seen.push("on"); };
        worker.on("tick", onTick);
        worker.emit("tick");
        worker.off("tick", onTick);
        worker.emit("tick");
        worker.once("tick", () => { seen.push("once"); });
        worker.emit("tick");
        worker.emit("tick");
        worker.emit("pre", 7);
        worker.on("pre", (value) => { seen.push("pre" + value); });
        worker.addListener("alias", () => { seen.push("alias"); });
        worker.emit("alias");
        worker.removeListener("alias", () => {});
        worker.removeAllListeners("alias");
        worker.emit("alias");
        worker.on("x", () => { seen.push("x"); });
        worker.on("y", () => { seen.push("y"); });
        worker.removeAllListeners();
        worker.emit("x");
        worker.onmessage = () => { seen.push("onmsg"); };
        const onmessageHandler = worker.onmessage;
        worker.addEventListener("message", () => { seen.push("dom"); });
        worker.dispatchEvent({ type: "message", data: 1 });
        worker.onerror = () => { seen.push("err"); };
        const onerrorHandler = worker.onerror;
        worker.dispatchEvent({ type: "error", message: "nope" });
        worker.removeEventListener("message", () => {});
        worker.ref();
        worker.unref();
        markAsUncloneable({ payload: 1 });
        const threadId = worker.threadId;
        worker.on("exit", (code) => {
          console.log(
            seen.join("+"),
            threadId > 0,
            typeof onmessageHandler === "function",
            typeof onerrorHandler === "function",
            code,
          );
        });
      } else {
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "on+once+pre7+alias+onmsg+dom+err true true true 0",
    );
  });

  it("supports MessagePort methods in both directions", () => {
    const source = `
      import { MessageChannel, MessagePort, receiveMessageOnPort } from "node:worker_threads";
      const channel = new MessageChannel();
      const port1 = channel.port1;
      const port2 = channel.port2;
      const seen: string[] = [];
      port1.onmessage = (event) => { seen.push("a" + event.data.n); };
      port2.on("message", (value) => { seen.push("b" + value.n); });
      port1.start();
      port2.start();
      port1.ref();
      port1.unref();
      port1.postMessage({ n: 1 });
      port2.postMessage({ n: 2 });
      try {
        new MessagePort();
      } catch (error) {
        console.log("ctor", (error as Error).message.includes("not constructible"));
      }
      console.log("sync", receiveMessageOnPort(port1) === undefined, port2 === channel.port2);
      setTimeout(() => {
        port1.close();
        port2.close();
        console.log(seen.sort().join(","), "closed");
      }, 10);
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "ctor true\nsync true true\na2,b1 closed",
    );
  });

  it("reports postMessage on an exited worker and terminate's value", () => {
    const source = `
      import { Worker, isMainThread } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("exit", (code) => {
          let threw = false;
          try {
            worker.postMessage("late");
          } catch (error) {
            threw = (error as Error).message.includes("not running");
          }
          worker.terminate().then((value) => {
            console.log("late", code, threw, typeof value, value === 0);
          });
        });
      } else {
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("late 0 true number true");
  });

  it("rejects uncloneable values before sending", () => {
    const source = `
      import { Worker, isMainThread, MessageChannel } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        const port = new MessageChannel().port1;
        const values: any[] = [Symbol("s"), Promise.resolve(1), new WeakMap(), new WeakSet(), port];
        for (const value of values) {
          try {
            worker.postMessage(value);
            console.log("accepted");
          } catch (error) {
            console.log("rejected", (error as Error).message.includes("could not be cloned"));
          }
        }
        worker.terminate();
      } else {
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "rejected true\nrejected true\nrejected true\nrejected true\nrejected true",
    );
  });

  it("sends through the bare Web Worker postMessage global", () => {
    const source = `
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.onmessage = (event) => { console.log("msg", event.data); };
      } else {
        postMessage("hi");
        self.close();
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("msg hi");
  });

  it("rejects constructing the Web Worker MessagePort global", () => {
    const source = `
      const channel = new MessageChannel();
      channel.port2.onmessage = (event) => { console.log("port", event.data); };
      channel.port1.postMessage(5);
      try {
        new MessagePort();
      } catch (error) {
        console.log("ctor", (error as Error).message.includes("not constructible"));
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("ctor true\nport 5");
  });

  it("exposes the parentPort scope API", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("message", (summary) => { console.log(summary); });
      } else {
        const seen: string[] = [];
        const handler = () => { seen.push("custom"); };
        parentPort.addEventListener("custom", handler);
        parentPort.dispatchEvent({ type: "custom" });
        parentPort.removeEventListener("custom", handler);
        parentPort.once("later", () => { seen.push("late"); });
        parentPort.emit("later");
        parentPort.emit("later");
        parentPort.on("off-me", () => { seen.push("off"); });
        parentPort.off("off-me", () => {});
        parentPort.on("a", () => {});
        parentPort.removeAllListeners();
        parentPort.start();
        parentPort.ref();
        parentPort.unref();
        parentPort.onmessage = () => { seen.push("onmsg"); };
        parentPort.dispatchEvent({ type: "message", data: "x" });
        parentPort.postMessage(seen.join("+"));
        parentPort.close();
        try {
          parentPort.postMessage("late");
        } catch (error) {
          seen.push("caught");
        }
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("custom+late+onmsg");
  });

  it("delivers a large message while closing the port", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("message", (value) => {
          console.log("big", value.length, value.charCodeAt(0) === 120, value.charCodeAt(value.length - 1) === 120);
        });
      } else {
        parentPort.postMessage("x".repeat(4000000));
        parentPort.close();
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("big 4000000 true true");
  });

  it("keeps a waiting worker alive across poll ticks and terminates it", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("exit", (code) => { console.log("exit", code); });
        setTimeout(() => {
          worker.terminate().then((value) => { console.log("terminated", value); });
        }, 120);
      } else {
        parentPort.on("message", () => {});
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("exit 1\nterminated 1");
  });

  it("keeps the worker target rooted across collections", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        let count = 0;
        worker.on("message", (value) => {
          count++;
          if (count === 3) {
            console.log("gc", value, count);
            worker.terminate();
          } else {
            worker.postMessage(count);
          }
        });
        worker.postMessage(0);
        const junk: string[] = [];
        for (let i = 0; i < 100; i++) junk.push("garbage" + i);
        console.log("allocated", junk.length);
      } else {
        parentPort.on("message", (value) => { parentPort.postMessage(value + 1); });
      }
    `;
    expect(runProgram(source, { extensions: true, env: { XT_GC_THRESHOLD: "1" } })).toBe(
      "allocated 100\ngc 3 3",
    );
  });

  it("requires a script URL", () => {
    const source = `
      import { Worker, isMainThread } from "node:worker_threads";
      if (isMainThread) {
        try {
          (Worker as any)();
        } catch (error) {
          console.log("script", (error as Error).message.includes("requires a script URL"));
        }
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("script true");
  });

  it("reports postMessage outside a worker", () => {
    const source = `
      try {
        postMessage("main");
      } catch (error) {
        console.log("main", (error as Error).message.includes("only available inside a worker"));
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("main true");
  });

  it("supports the webworker module Worker export", () => {
    const source = `
      import { Worker, isMainThread, postMessage } from "webworker";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.onmessage = (event) => { console.log("msg", event.data); };
      } else {
        postMessage("hi");
        self.close();
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("msg hi");
  });

  it("supports the webworker module MessagePort exports", () => {
    const source = `
      import { MessageChannel, MessagePort } from "webworker";
      const channel = new MessageChannel();
      channel.port2.onmessage = (event) => { console.log("port", event.data); };
      channel.port1.postMessage(9);
      try {
        new MessagePort();
      } catch (error) {
        console.log("ctor", (error as Error).message.includes("not constructible"));
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("ctor true\nport 9");
  });

  it("reports a disabled WebAssembly feature as unsupported", () => {
    const source = `
      try {
        WebAssembly.validate(new Uint8Array([0]));
      } catch (error) {
        console.log("validate", (error as Error).message.includes("not supported"));
      }
      try {
        new WebAssembly.Module(new Uint8Array([0]));
      } catch (error) {
        console.log("module", (error as Error).message.includes("not supported"));
      }
      console.log(typeof WebAssembly.compile, typeof WebAssembly.CompileError);
    `;
    expect(runProgram(source, { noFeatures: ["wasm"] })).toBe(
      "validate true\nmodule true\nfunction function",
    );
  });

  it.runIf(process.platform === "win32")("rejects workerData over the Windows environment limit", () => {
    const source = `
      import { Worker, isMainThread } from "node:worker_threads";
      if (isMainThread) {
        try {
          new Worker(import.meta.filename, { workerData: "x".repeat(40000) });
          console.log("started");
        } catch (error) {
          console.log("large", (error as Error).message.includes("too large"));
        }
      } else {
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("large true");
  });
});
