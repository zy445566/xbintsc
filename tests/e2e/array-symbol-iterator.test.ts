/**
 * `Array.prototype[Symbol.iterator]` — the explicit iterator protocol.
 *
 * `for...of` and spread use the internal index path, but library code (and
 * manual `.next()` loops) calls the method directly. It used to be undefined,
 * so `arr[Symbol.iterator]()` threw.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("array Symbol.iterator", (harness) => {
  it("returns a working iterator with a stable method value", () => {
    harness.expectSameOutputAsNode(`
      const iterator = [10, 20][Symbol.iterator]();
      console.log(iterator.next().value, iterator.next().value, iterator.next().done);

      const values = [1, 2, 3];
      console.log(values[Symbol.iterator] === values[Symbol.iterator]);
      console.log([...values[Symbol.iterator]()].join(","));

      const manual: number[] = [];
      for (const value of values[Symbol.iterator]()) manual.push(value);
      console.log(manual.join("+"));
    `);
  });

  it("lets an own symbol property override the built-in method", () => {
    harness.expectSameOutputAsNode(`
      const values: any = [1];
      values[Symbol.iterator] = function () {
        let index = 0;
        return {
          next: function () {
            index++;
            if (index === 1) return { value: "custom", done: false };
            return { value: undefined, done: true };
          },
        };
      };
      console.log(values[Symbol.iterator]().next().value);
    `);
  });
});
