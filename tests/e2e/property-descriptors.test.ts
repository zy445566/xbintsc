/**
 * Object.defineProperty / getOwnPropertyDescriptor(s): data and accessor
 * property definitions plus the array `length` descriptor.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("property descriptors", (harness) => {
  it("defines and reads data and accessor properties like Node", () => {
    harness.expectSameOutputAsNode(`
      const object: any = { a: 1 };
      Object.defineProperty(object, "b", { value: 2 });
      console.log(object.b, JSON.stringify(Object.getOwnPropertyDescriptor(object, "b")));
      console.log(Object.getOwnPropertyDescriptor(object, "missing"));

      const accessor: any = {};
      Object.defineProperty(accessor, "x", { get: () => 42 });
      console.log(accessor.x, typeof Object.getOwnPropertyDescriptor(accessor, "x").get);

      const all: any = Object.getOwnPropertyDescriptors({ a: 1 });
      console.log(all.a.value, all.a.writable);
      console.log(JSON.stringify(Object.getOwnPropertyDescriptor([1, 2], "length")));
    `);
  });
});
