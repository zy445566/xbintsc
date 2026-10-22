/**
 * `structuredClone`: deep clone with shared references and cycles, over the
 * data types the runtime represents (arrays, objects, Map, Set, Date, RegExp,
 * typed arrays). Functions, symbols and promises are rejected.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("structuredClone", (harness) => {
  it("deep clones data containers and preserves cycles", () => {
    harness.expectSameOutputAsNode(`
      const source: any = {
        a: 1,
        nested: { b: [1, 2] },
        map: new Map([["k", { v: 2 }]]),
        set: new Set([1]),
        date: new Date(0),
        re: /ab/gi,
      };
      source.self = source;
      const clone: any = structuredClone(source);
      console.log(clone !== source, clone.self === clone, clone.nested.b.join(","));
      console.log(clone.map.get("k").v, clone.set.has(1), clone.date.toISOString(), clone.re.source, clone.re.flags);

      clone.nested.b.push(3);
      console.log(source.nested.b.length, clone.nested.b.length);

      const shared = { x: 1 };
      const pair: any = { left: shared, right: shared };
      const clonedPair: any = structuredClone(pair);
      console.log(clonedPair.left === clonedPair.right);

      console.log(structuredClone(5), structuredClone("x"));
      const bytes: any = structuredClone(new Uint8Array([1, 2]));
      console.log(bytes instanceof Uint8Array, bytes.join(","));
    `);
  });

  it("rejects values that cannot be cloned", () => {
    harness.expectSameOutputAsNode(`
      let threw = false;
      try {
        (structuredClone as any)(function named() {});
      } catch {
        threw = true;
      }
      console.log(threw);
    `);
  });
});
