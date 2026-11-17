/**
 * End-to-end coverage for objects whose prototype is not a plain object.
 *
 * `Object.setPrototypeOf(o, new Map())` is legal JavaScript. The property walk
 * must stop at a container whose representation is not an `xt_object` property
 * table; reading it as one used to walk past the allocation.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end prototype-chain safety", (harness) => {
  const { runProgram } = harness;

  it("does not walk a non-plain object as a property table", () => {
    const source = `
      const o: any = { a: 1 };
      Object.setPrototypeOf(o, new Map());
      console.log(o.a, o.b);
      const p: any = { x: 2 };
      Object.setPrototypeOf(p, []);
      console.log(p.x, p.y);
    `;
    expect(runProgram(source)).toBe("1 undefined\n2 undefined");
  });
});
