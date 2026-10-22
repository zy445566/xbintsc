/**
 * `await` always yields once, even for a non-promise or an already-settled
 * promise, so it interleaves with pending `.then` callbacks exactly like Node.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("await microtask ordering", (harness) => {
  it("yields for non-promises and settled promises", () => {
    harness.expectSameOutputAsNode(`
      async function run(): Promise<void> {
        console.log("a");
        await 1;
        console.log("b");
        await Promise.resolve(2);
        console.log("c");
      }
      run();
      console.log("caller");
      Promise.resolve().then(() => console.log("then1"));
    `);
  });

  it("queues rejection handling in the same order as Node", () => {
    harness.expectSameOutputAsNode(`
      async function run(): Promise<void> {
        try {
          await Promise.reject("boom");
        } catch (error) {
          console.log("caught", error);
        }
      }
      run();
      console.log("sync");
      Promise.resolve().then(() => console.log("then"));
    `);
  });
});
