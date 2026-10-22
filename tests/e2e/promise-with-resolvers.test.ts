/**
 * Promise.withResolvers(): a pending promise plus its resolve/reject functions,
 * for APIs that hand the promise out before it settles.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Promise.withResolvers", (harness) => {
  it("resolves and rejects through the returned functions", () => {
    harness.expectSameOutputAsNode(`
      const first = (Promise as any).withResolvers();
      first.promise.then((value: number) => console.log("resolved", value));
      first.resolve(42);

      const second = (Promise as any).withResolvers();
      second.promise.catch((error: string) => console.log("rejected", error));
      second.reject("nope");
    `);
  });
});
