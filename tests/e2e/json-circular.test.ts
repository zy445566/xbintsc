/**
 * `JSON.stringify` must reject a circular structure with a TypeError instead of
 * recursing until the native stack overflows (which crashed the process with
 * 0xC00000FD / SIGSEGV).
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.stringify circular structures", (harness) => {
  it("throws a TypeError for circular objects and arrays", () => {
    harness.expectSameOutputAsNode(`
      const object: any = { a: 1 };
      object.self = object;
      try {
        JSON.stringify(object);
      } catch (e: any) {
        console.log("object", e instanceof TypeError, e.name);
      }

      const list: any[] = [];
      list.push(list);
      try {
        JSON.stringify(list);
      } catch (e: any) {
        console.log("array", e instanceof TypeError, e.name);
      }
    `);
  });

  it("still serializes deep acyclic values and repeated (non-circular) ones", () => {
    harness.expectSameOutputAsNode(`
      const deep: any = { v: 1 };
      let cursor = deep;
      for (let i = 0; i < 50; i++) {
        cursor.next = { v: i };
        cursor = cursor.next;
      }
      console.log(JSON.stringify(deep).length > 100);

      const shared = { x: 1 };
      console.log(JSON.stringify({ a: shared, b: shared }));
    `);
  });
});
