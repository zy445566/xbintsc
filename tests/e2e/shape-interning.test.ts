/**
 * Hidden-class (shape) interning.
 *
 * A shape is interned by `(parent shape, property key)`. The key has to be
 * compared by *text*, not by the address of the string that holds it: property
 * keys built at run time — a typed array's `byteLength`, a DOM handle's
 * `length`, any `obj["k" + i]` — are fresh strings with the same bytes. When
 * only the pointer was compared, every insert of such a key interned a new
 * shape; past the intern limit new objects got no shape at all, which disabled
 * the property and method inline caches for the whole program.
 *
 * This is observable from the outside through the `XT_SHAPE_TRACE` diagnostic,
 * which reports the interned-transition count as it grows, so the regression is
 * guarded without measuring time.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("hidden-class interning", (harness) => {
  const { runProgramFull } = harness;

  it("interns run-time property keys by text, not by string identity", () => {
    const source = `
      const arrays: Float32Array[] = [];
      let total = 0;
      for (let i = 0; i < 20000; i++) {
        const a = new Float32Array(3);
        a[0] = i;
        arrays.push(a);
        total += a[0];
      }
      console.log(arrays.length, total);
    `;
    const result = runProgramFull(source, { env: { XT_SHAPE_TRACE: "1" } });
    expect(result.status).toBe(0);
    expect(result.stdout.trim()).toBe(`20000 ${(19999 * 20000) / 2}`);
    /* A pointer-keyed table needs five transitions per array — 100 000 in
     * total, so the diagnostic fires at the 32 768 mark. A value-keyed table
     * needs one layout (five transitions) for all of them. */
    expect(result.stderr).not.toContain("xt_shape:");
  });

  it("still interns literal property keys that a program touches directly", () => {
    const source = `
      class Vec { constructor(public x = 0, public y = 0, public z = 0) {} }
      let sum = 0;
      for (let i = 0; i < 20000; i++) {
        const v = new Vec(i, i + 1, i + 2);
        sum += v.x + v.y + v.z;
      }
      console.log(sum);
    `;
    const result = runProgramFull(source, { env: { XT_SHAPE_TRACE: "1" } });
    expect(result.status).toBe(0);
    expect(result.stderr).not.toContain("xt_shape:");
  });
});
