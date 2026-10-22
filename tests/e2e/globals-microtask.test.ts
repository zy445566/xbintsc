/**
 * Node globals that sit on the event loop: `queueMicrotask`, `setImmediate`,
 * `clearImmediate` and `performance.now`.
 *
 * queueMicrotask shares the promise reaction queue (FIFO), setImmediate is a
 * zero-delay timer that forwards extra arguments, and performance.now reads a
 * monotonic clock with sub-millisecond resolution.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("microtask and immediate globals", (harness) => {
  it("orders microtasks, supports first-class values and performance.now", () => {
    harness.expectSameOutputAsNode(`
      queueMicrotask(() => console.log("micro"));
      Promise.resolve().then(() => console.log("promise"));
      queueMicrotask(() => console.log("micro2"));

      console.log(typeof performance.now(), performance.now() > 0, typeof performance.now);
      const now = performance.now;
      console.log(typeof now);

      setImmediate((a: number, b: number) => console.log("immediate", a, b), 1, 2);
      const cancelled = setImmediate(() => console.log("cancelled"));
      clearImmediate(cancelled);
    `);
  });

  it("rejects a non-function microtask callback", () => {
    harness.expectSameOutputAsNode(`
      try {
        queueMicrotask(1 as any);
      } catch (e: any) {
        console.log(e instanceof TypeError, e.name);
      }
    `);
  });
});
