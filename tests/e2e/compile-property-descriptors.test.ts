/**
 * End-to-end test for property descriptors without a `value` field.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end property descriptors", (harness) => {
  const { runProgram } = harness;

  /* A descriptor with no `value` must not write the property. The runtime's
   * `if (xt_object_has_own(...))` tested the NaN-boxed xt_value directly, so
   * every descriptor looked like a data descriptor and an existing setter ran
   * with `undefined` (undici installs its WebSocket accessors this way). */
  it("does not write a property for a descriptor without a value", () => {
    const source = `
      class Box {
        seen: string[] = [];
        get label() { return "value"; }
        set label(next: string) { this.seen.push(String(next)); }
      }
      Object.defineProperty(Box.prototype, "label", {});
      Object.defineProperty(Box.prototype, "label", { enumerable: true });
      Object.defineProperty(Box.prototype, "extra", { value: 7 });
      const box = new Box();
      console.log(box.seen.length, box.label, (box as unknown as { extra: number }).extra);
    `;
    expect(runProgram(source)).toBe("0 value 7");
  });
});
