/**
 * JSON.stringify replacer: an array filters object keys; a function receives
 * (key, value) with the holder as `this`, may return undefined to omit a
 * property, and toJSON is applied first.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.stringify replacer", (harness) => {
  it("filters keys, transforms values and honours toJSON", () => {
    harness.expectSameOutputAsNode(`
      const data: any = { a: 1, b: 2, nested: { c: 3 }, list: [1, 2] };

      console.log(JSON.stringify(data, ["a", "nested"]));
      console.log(JSON.stringify(data, (key: string, value: any) =>
        key === "b" ? undefined : typeof value === "number" ? value * 10 : value));
      console.log(JSON.stringify({ d: new Date(0) }, (key: string, value: any) => value));

      const custom: any = {
        x: 1,
        toJSON(key: string) { return { k: key, x: this.x }; },
      };
      console.log(JSON.stringify(custom, (key: string, value: any) => value));
      console.log(JSON.stringify(data, (key: string, value: any) => value, 2));
    `);
  });
});
