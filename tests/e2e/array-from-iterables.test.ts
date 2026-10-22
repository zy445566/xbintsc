/**
 * `Array.from` over iterables. It only consumed arrays, strings and
 * array-likes, so `Array.from(new Set(...))`, `Array.from(map)`,
 * `Array.from(generator())` and custom iterables silently returned `[]` even
 * though `for...of` worked.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Array.from iterables", (harness) => {
  it("consumes Map, Set, generators and custom iterables", () => {
    harness.expectSameOutputAsNode(`
      console.log(Array.from(new Set([1, 2, 2])).join(","));
      console.log(Array.from(new Map([["a", 1], ["b", 2]])).map((pair: any) => pair.join(":")).join(","));

      function* numbers() { yield 1; yield 2; }
      console.log(Array.from(numbers()).join("-"));

      const iterable: any = {
        index: 0,
        next: function () {
          this.index++;
          return { value: this.index, done: this.index > 2 };
        },
      };
      iterable[Symbol.iterator] = function () { return this; };
      console.log(Array.from(iterable).join(","));

      console.log(Array.from({ 0: "a", 1: "b", length: 2 }).join(""));
      console.log(Array.from("😀a").join("|"));
      console.log(Array.from(new Set([1, 2]), (value: number) => value * 2).join(","));
    `);
  });
});
