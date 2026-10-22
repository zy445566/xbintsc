/**
 * `console.log` inspection of the runtime's built-in object kinds.
 *
 * Map / Set / Date / RegExp / Promise / generator values used to be treated as
 * ordinary property-bag objects, so printing a non-empty Map, Set, RegExp,
 * Promise or generator read past the end of its struct and crashed the process
 * with an access violation. These tests pin the Node-compatible formatting and
 * the no-crash guarantee.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("console inspection of built-ins", (harness) => {
  it("formats Map, Set, Date, RegExp, Promise and generators like Node", () => {
    harness.expectSameOutputAsNode(`
      console.log(new Map([["a", 1], [2, "two"]]));
      console.log(new Map());
      console.log(new Set([1, 2]));
      console.log(new Set());
      console.log(new Date(0));
      console.log(/ab/gi);
      console.log(Promise.resolve(1));
      console.log((function* () { yield 1; })());
      console.log({ m: new Map([["a", 1]]) });
    `);
  });

  it("prints nested Map and Set values without crashing", () => {
    harness.expectSameOutputAsNode(`
      const inner = new Map([["x", 1]]);
      console.log([inner, new Set(["a"])]);
    `);
  });

  it("marks self-referential Maps and Sets instead of recursing", () => {
    expect(
      harness.runProgram(`
        const map: any = new Map();
        map.set("self", map);
        console.log(map);
        const set: any = new Set();
        set.add(set);
        console.log(set);
      `),
    ).toBe("Map(1) { 'self' => [Circular *1] }\nSet(1) { [Circular *1] }");
  });
});
