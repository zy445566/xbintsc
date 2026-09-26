/**
 * Differential tests for symbols and namespace imports.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("differential vs Node: symbols and bindings", (harness) => {
  it("implements symbols and the well-known symbol protocol", () => {
    harness.expectSameOutputAsNode(
      `const a = Symbol("desc");
const b = Symbol("desc");
const lines: unknown[] = [
  typeof Symbol,
  typeof a,
  a === a,
  a === b,
  a.toString(),
  String(a),
  a.description,
  Symbol().description,
  String(Symbol()),
  String(Symbol.iterator),
  Symbol.iterator === Symbol.iterator,
  Symbol.for("k") === Symbol.for("k"),
  Symbol.keyFor(Symbol.for("k")),
  Symbol.keyFor(Symbol("k")),
];
for (const line of lines) console.log(JSON.stringify(line));

const holder: Record<string, unknown> = {};
(holder as any)[a] = 1;
(holder as any)[b] = 2;
holder.x = 3;
console.log(JSON.stringify(Object.keys(holder)));
console.log(JSON.stringify(Object.getOwnPropertySymbols(holder).length));
console.log(
  JSON.stringify([(holder as any)[a], (holder as any)[b], (holder as any)[Symbol("desc")]]),
);
console.log(JSON.stringify(holder));
console.log(JSON.stringify(a));
console.log(JSON.stringify([a, 1]));

class Range {
  start: number;
  end: number;
  constructor(start: number, end: number) {
    this.start = start;
    this.end = end;
  }
  [Symbol.iterator]() {
    let i = this.start;
    const end = this.end;
    return {
      next() {
        return i < end ? { value: i++, done: false } : { value: undefined, done: true };
      },
    };
  }
}
console.log(JSON.stringify([...new Range(1, 4)]));
const walked: number[] = [];
for (const x of new Range(10, 13)) walked.push(x);
console.log(JSON.stringify(walked));

function* delegates() { yield* new Range(5, 7); }
console.log(JSON.stringify([...delegates()]));
`,
      { name: "diff_symbol" },
    );
  });

  it("binds namespace imports to every export", () => {
    harness.expectSameOutputAsNode(
      `import * as util from "./util.ts";
import { add } from "./util.ts";
const values: unknown[] = [
  util.greeting,
  util.add(2, 3),
  add(20, 22),
  new util.default().value,
  Object.keys(util).sort(),
  typeof util.add,
];
for (const v of values) console.log(JSON.stringify(v));
`,
      {
        name: "diff_namespace_import",
        files: {
          "util.ts": [
            'export const greeting = "hello";',
            "export function add(a: number, b: number): number { return a + b; }",
            "export default class Thing { value = 42; }",
            "export const ignored = 1;\n",
          ].join("\n"),
        },
      },
    );
  });
});
