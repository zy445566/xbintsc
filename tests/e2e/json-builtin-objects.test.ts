/**
 * `JSON.stringify` of built-in objects.
 *
 * Map/Set/RegExp/Promise/generator values are not property-bag objects, but the
 * serializer handed them to the generic object path, which read their struct
 * fields as a property table and crashed with an access violation. JSON sees no
 * enumerable own properties on them; Date goes through toJSON.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.stringify built-in values", (harness) => {
  it("serializes Map, Set, RegExp, Date and Promise like Node", () => {
    harness.expectSameOutputAsNode(`
      console.log(JSON.stringify(new Map([["a", 1]])));
      console.log(JSON.stringify(new Set([1, 2])));
      console.log(JSON.stringify(/ab/gi));
      console.log(JSON.stringify(new Date(0)));
      console.log(JSON.stringify(Promise.resolve(1)));
      console.log(JSON.stringify({ map: new Map([["a", 1]]), date: new Date(0) }));
    `);
  });
});
