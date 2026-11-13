/**
 * End-to-end tests for the Web Worker extension (`--ext webworker`).
 *
 * The worker script is the running executable itself, so a program branches on
 * the `isMainThread` global instead of living in a second file.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Web Worker", (harness) => {
  const { runProgram } = harness;

  it("answers through self.onmessage and self.postMessage", () => {
    const source = `
      if (isMainThread) {
        const worker = new Worker(import.meta.filename, { name: "echo" });
        worker.onmessage = (event) => {
          console.log("reply", event.data, event.type);
        };
        worker.postMessage("ping");
      } else {
        self.onmessage = (event) => {
          self.postMessage(event.data + ":" + self.name);
          self.close();
        };
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("reply ping:echo message");
  });

  it("supports addEventListener and MessageChannel", () => {
    const source = `
      const channel = new MessageChannel();
      channel.port2.addEventListener("message", (event) => {
        console.log("port", event.data.n, event.type);
      });
      channel.port1.postMessage({ n: 5 });
    `;
    expect(runProgram(source, { webworker: true })).toBe("port 5 message");
  });

  it("reports worker errors and clones function-free data", () => {
    const source = `
      if (isMainThread) {
        const worker = new Worker(import.meta.filename, { workerData: { n: 2 } });
        worker.addEventListener("message", (event) => {
          console.log("data", event.data.total);
          worker.terminate();
        });
        worker.postMessage("go");
      } else {
        self.onmessage = () => {
          self.postMessage({ total: 2 + 3 });
        };
      }
    `;
    expect(runProgram(source, { webworker: true })).toBe("data 5");
  });

  it("shares the transport with node:worker_threads", () => {
    const source = `
      import { isMainThread as nodeMainThread } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename);
        worker.onmessage = (event) => {
          console.log(event.data, nodeMainThread);
          worker.terminate();
        };
        worker.postMessage("ok");
      } else {
        self.onmessage = (event) => self.postMessage(event.data + "!");
      }
    `;
    expect(runProgram(source, { extensions: true, webworker: true })).toBe("ok! true");
  });
});
