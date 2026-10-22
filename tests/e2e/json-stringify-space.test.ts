/**
 * `JSON.stringify(value, replacer, space)` reads `space` from the third
 * argument and indents with it. The runtime used to read the second argument
 * (the replacer), so `JSON.stringify(value, null, 2)` silently stayed compact.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.stringify spacing", (harness) => {
  it("indents with a numeric or string space argument", () => {
    harness.expectSameOutputAsNode(`
      const value = { a: 1, b: [2, { c: 3 }], d: "x" };
      console.log(JSON.stringify(value, null, 2));
      console.log(JSON.stringify(value, null, 4));
      console.log(JSON.stringify(value, null, 20));
      console.log(JSON.stringify(value, null, "\\t"));
      console.log(JSON.stringify(value, null, "ab"));
      console.log(JSON.stringify(value, null, 0));
      console.log(JSON.stringify({}, null, 2));
      console.log(JSON.stringify([], null, 3));
    `);
  });
});
