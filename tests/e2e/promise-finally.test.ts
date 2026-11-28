/**
 * End-to-end tests for `Promise.prototype.finally` and throwing promise
 * callbacks.
 *
 * `finally` used to run its handler synchronously and return the receiver, so
 * the derived promise was missing and a throwing handler escaped as an
 * uncaught exception. A `.then`/`.catch` callback that throws had the same
 * problem: the exception escaped the microtask queue instead of rejecting the
 * derived promise (coding-agent's model-catalog refresh hit both).
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Promise.finally and throwing reactions", (harness) => {
  const { runProgram, expectSameOutputAsNode } = harness;

  it("runs finally in a microtask and preserves the value", () => {
    const source = `
      const log: string[] = [];
      const result = Promise.resolve("value")
        .finally(() => { log.push("finally"); })
        .then((value) => { log.push("then:" + value); return value; });
      log.push("sync");
      result.then(() => console.log(log.join(",")));
    `;
    expectSameOutputAsNode(source, { name: "promise-finally-order" });
  });

  it("keeps the rejection and runs a throwing finally as a rejection", () => {
    const source = `
      Promise.reject(new Error("original"))
        .finally(() => { throw new Error("from finally"); })
        .catch((error: Error) => console.log("caught", error.message));
      Promise.resolve(1)
        .finally(() => { console.log("cleanup"); })
        .catch(() => console.log("unreachable"));
    `;
    expect(runProgram(source)).toBe("cleanup\ncaught from finally");
  });

  it("rejects the derived promise when a then/catch callback throws", () => {
    const source = `
      Promise.resolve(1)
        .then(() => { throw new Error("then boom"); })
        .catch((error: Error) => console.log("then caught", error.message));
      Promise.reject(new Error("boom"))
        .catch((error: Error) => { throw error; })
        .catch((error: Error) => console.log("catch caught", error.message));
    `;
    expect(runProgram(source)).toBe("then caught then boom\ncatch caught boom");
  });
});
