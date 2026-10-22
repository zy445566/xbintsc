/**
 * Calling a property that is not a function must throw, even on primitives.
 *
 * Array / String / Number / Boolean / BigInt method dispatch used to return
 * `undefined` when no table matched, so `"x".nope()`, `[].nope()` and
 * `(1).nope()` silently produced `undefined` instead of a TypeError. That hid
 * typos and every unimplemented standard method (e.g. `String.normalize`).
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("missing method calls throw", (harness) => {
  it("throws a TypeError for unknown methods on primitives and arrays", () => {
    harness.expectSameOutputAsNode(`
      function probe(label: string, fn: () => void): void {
        try {
          fn();
          console.log(label, "no throw");
        } catch (e: any) {
          console.log(label, e instanceof Error, e instanceof TypeError, e.name);
        }
      }
      probe("string", () => { ("x" as any).nope(); });
      probe("array", () => { ([] as any).nope(); });
      probe("number", () => { (1 as any).nope(); });
      probe("boolean", () => { (true as any).nope(); });
      probe("bigint", () => { (1n as any).nope(); });
    `);
  });

  it("leaves known methods and optional calls working", () => {
    harness.expectSameOutputAsNode(`
      console.log("x".toUpperCase(), [1, 2].map((x: number) => x * 2).join(","));
      console.log((255).toString(16), true.toString(), (10n).toString());
      const object: any = { fn: () => 1 };
      console.log(object.fn?.(), object.missing?.());
    `);
  });
});
