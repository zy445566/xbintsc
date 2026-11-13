/**
 * End-to-end tests for `node:worker_threads`.
 *
 * xbintsc emulates a worker with a second process running the same executable,
 * so every program guards its body with `isMainThread` and uses
 * `import.meta.filename` as the worker script.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("worker_threads", (harness) => {
  const { runProgram } = harness;

  it("relays messages both ways with structured workerData", () => {
    const source = `
      import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename, { workerData: { base: 10 } });
        worker.on("message", (msg) => {
          console.log(JSON.stringify({ sum: msg.sum, base: msg.base, bytes: [msg.bytes[0], msg.bytes[1]], when: msg.when.getTime() }));
          worker.terminate();
        });
        worker.postMessage({ values: [1, 2, 3] });
      } else {
        parentPort.on("message", (msg) => {
          parentPort.postMessage({
            sum: msg.values[0] + msg.values[1] + msg.values[2],
            base: workerData.base,
            bytes: new Uint8Array([7, 9]),
            when: new Date(1000),
          });
        });
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      '{"sum":6,"base":10,"bytes":[7,9],"when":1000}',
    );
  });

  it("emits online, message and exit in order", () => {
    const source = `
      import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        const seen: string[] = [];
        worker.on("online", () => { seen.push("online"); });
        worker.on("message", (msg) => { seen.push("message:" + msg); });
        worker.on("exit", (code) => {
          seen.push("exit:" + code);
          console.log(seen.join(","));
        });
        worker.postMessage("ping");
      } else {
        parentPort.on("message", (msg) => {
          parentPort.postMessage(msg + "!");
          parentPort.close();
        });
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("online,message:ping!,exit:0");
  });

  it("delivers MessageChannel messages asynchronously", () => {
    const source = `
      import { MessageChannel } from "node:worker_threads";
      const channel = new MessageChannel();
      channel.port2.on("message", (value) => {
        console.log("received", value.text, value.items.length);
      });
      channel.port1.postMessage({ text: "local", items: [1, 2, 3] });
      console.log("posted");
    `;
    expect(runProgram(source, { extensions: true })).toBe("posted\nreceived local 3");
  });

  it("runs several workers in one program", () => {
    const source = `
      import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
      if (isMainThread) {
        const replies: string[] = [];
        const workers: Worker[] = [];
        for (let i = 0; i < 3; i++) {
          const worker = new Worker(import.meta.filename, { workerData: i });
          workers.push(worker);
          worker.on("message", (msg) => {
            replies.push(msg);
            if (replies.length === 3) {
              console.log(replies.sort().join(","));
              for (const each of workers) each.terminate();
            }
          });
        }
      } else {
        parentPort.postMessage("worker-" + workerData);
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("worker-0,worker-1,worker-2");
  });

  it("round-trips structured clone types through a worker", () => {
    const source = `
      import { Worker, isMainThread, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.on("message", (msg) => {
          const cyclic: any = msg.self;
          console.log(
            msg.date.getTime(),
            msg.reg.source,
            msg.reg.flags,
            msg.map.get("k"),
            msg.set.has(2),
            msg.big === 123n,
            cyclic.self === cyclic,
            msg.buffer.byteLength,
            msg.floats[1],
          );
          worker.terminate();
        });
        worker.postMessage("go");
      } else {
        parentPort.on("message", () => {
          const cyclic: any = { name: "c" };
          cyclic.self = cyclic;
          parentPort.postMessage({
            date: new Date(2000),
            reg: /ab+c/gi,
            map: new Map([["k", 7]]),
            set: new Set([1, 2]),
            big: 123n,
            self: cyclic,
            buffer: new ArrayBuffer(4),
            floats: new Float32Array([1.5, 2.5]),
          });
        });
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("2000 ab+c gi 7 true true true 4 2.5");
  });

  it("rejects a worker script that is not part of the binary", () => {
    const source = `
      import { Worker, isMainThread } from "node:worker_threads";
      if (isMainThread) {
        try {
          new Worker("./not-in-this-binary.js");
        } catch (error) {
          console.log("rejected", (error as Error).message.includes("not part of this binary"));
        }
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("rejected true");
  });

  it("rejects an uncloneable message before sending", () => {
    const source = `
      import { Worker, isMainThread } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        try {
          worker.postMessage(() => 1);
        } catch (error) {
          console.log("rejected", (error as Error).message.includes("could not be cloned"));
        }
        worker.terminate();
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("rejected true");
  });
});
