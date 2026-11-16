/**
 * End-to-end tests for Node-extension interoperability that spans the class
 * system and the global object: extending a native class (`EventEmitter`) and
 * using `globalThis` / Node's `global` alias as a value.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (native classes and globals)", (harness) => {
  const { runProgram } = harness;

  it("extends a native EventEmitter and reaches the inherited API", () => {
    const source = `
      import { EventEmitter } from "events";
      class Counter extends EventEmitter {
        count = 0;
        constructor(private readonly step: number) {
          super();
        }
        bump(): number {
          this.count += this.step;
          this.emit("bump", this.count);
          return this.count;
        }
      }
      const counter = new Counter(2);
      let heard = 0;
      counter.on("bump", (value: number) => { heard = value; });
      console.log(counter.bump(), heard);
      console.log(counter instanceof Counter, typeof counter.on, typeof counter.listenerCount);
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      ["2 2", "true function function"].join("\n"),
    );
  });

  it("exposes globalThis and Node's global alias as one object", () => {
    const source = `
      const a = globalThis;
      const b = global;
      console.log(a === b);
      a.answer = 42;
      console.log(b.answer);
      const key = Symbol.for("xbintsc.global");
      b[key] = "stored";
      console.log(a[key]);
      console.log(typeof globalThis.missing);
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      ["true", "42", "stored", "undefined"].join("\n"),
    );
  });
});
