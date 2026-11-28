/**
 * End-to-end test for defaults nested in a class method's destructuring
 * parameter.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end class destructuring parameter defaults", (harness) => {
  const { runProgram } = harness;

  /* The binder skipped defaults nested in a class method's destructuring
   * parameter, so the identifiers resolved to undefined at run time. */
  it("reads defaults nested in a class method's destructuring parameter", () => {
    const source = `
      const FALLBACK = "fallback";
      const COUNT = 3;
      class Box {
        label: string;
        constructor({ label = FALLBACK }: { label?: string } = {}) { this.label = label; }
        count({ n = COUNT }: { n?: number } = {}): number { return n; }
      }
      const box = new Box();
      console.log(box.label, box.count(), box.count({ n: 9 }));
    `;
    expect(runProgram(source)).toBe("fallback 3 9");
  });
});
