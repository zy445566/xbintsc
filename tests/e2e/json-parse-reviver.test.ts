/**
 * JSON.parse reviver: the callback runs bottom-up with the holder as `this`,
 * receives string keys (including array indices), and returning undefined
 * deletes the property/element.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.parse reviver", (harness) => {
  it("transforms, deletes and receives `this` like Node", () => {
    harness.expectSameOutputAsNode(`
      const parsed: any = JSON.parse('{"a":1,"b":{"c":2},"list":[1,2]}', (key: string, value: any) => {
        if (key === "b") return undefined;
        if (typeof value === "number") return value * 10;
        return value;
      });
      console.log(JSON.stringify(parsed));

      console.log(JSON.stringify(JSON.parse("[1,2,3]", (key: string, value: any) => key === "1" ? undefined : value)));
      console.log(JSON.parse("5", (key: string, value: any) => key === "" ? value + 1 : value));

      const withThis: any = JSON.parse('{"a":1}', function (this: any, key: string, value: any) {
        if (key === "a") return this.a + 1;
        return value;
      });
      console.log(JSON.stringify(withThis));
    `);
  });
});
