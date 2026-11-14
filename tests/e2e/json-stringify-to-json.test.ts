/**
 * `JSON.stringify` must honour `toJSON` without a replacer.
 *
 * The replacer walk always ran the `toJSON` hook, but the plain fast path
 * dispatched on the value's kind and never looked for one, so `Date` only worked
 * because it had its own branch and every other object (a `Buffer`, a class with
 * `toJSON`) serialized as its property bag instead. The hook now runs first, as
 * SerializeJSONProperty prescribes, and receives the property key.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.stringify toJSON hook", (harness) => {
  it("calls toJSON with the property key and honours its result", () => {
    harness.expectSameOutputAsNode(`
      class Point {
        x: number;
        y: number;
        constructor(x: number, y: number) {
          this.x = x;
          this.y = y;
        }
        toJSON() {
          return { x: this.x, y: this.y, kind: "point" };
        }
      }
      console.log(JSON.stringify(new Point(1, 2)));
      console.log(JSON.stringify({ p: new Point(1, 2) }, null, 2));
      console.log(JSON.stringify(new Date(0)));
      console.log(JSON.stringify(new Date(NaN)));
      console.log(JSON.stringify({ iso: new Date(0), bad: new Date(NaN) }));
      console.log(JSON.stringify({ a: { toJSON: (key: string) => "key:" + key } }));
      console.log(JSON.stringify([{ toJSON: (key: string) => "key:" + key }]));
      console.log(JSON.stringify({ toJSON: (key: string) => "root:" + key }));
      console.log(JSON.stringify({ n: { toJSON: () => ({ deep: { toJSON: () => 7 } }) } }));
      console.log(JSON.stringify({ nan: { toJSON: () => NaN } }));
    `);
  });

  it("omits undefined results from objects and nulls them in arrays", () => {
    harness.expectSameOutputAsNode(`
      const drop = () => ({ toJSON: () => undefined });
      console.log(JSON.stringify({ drop: drop(), keep: 1 }));
      console.log(JSON.stringify({ only: drop() }));
      console.log(JSON.stringify([drop(), 2]));
      console.log(String(JSON.stringify({ toJSON: () => undefined })));
      const mixed: unknown[] = [1, () => 1, undefined, Symbol("s")];
      console.log(JSON.stringify(mixed));
      console.log(JSON.stringify({ arr: [{ toJSON: () => 1 }, 2] }, null, 1));
      console.log(JSON.stringify({ a: 1 }, ["a"], 2));
    `);
  });

  it("serializes Buffer through its toJSON hook", () => {
    harness.expectSameOutputAsNode(
      `
      const buffer = Buffer.from([65, 66]);
      console.log(JSON.stringify(buffer));
      console.log(JSON.stringify({ buffer }));
      console.log(JSON.stringify([buffer], null, 2));
      console.log(JSON.stringify({ skipped: buffer }, (key: string) => (key === "skipped" ? undefined : 1)));
    `,
      { extensions: true },
    );
  });
});
