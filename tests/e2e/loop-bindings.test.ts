/**
 * Per-iteration bindings for captured `let` / `const` in loops and blocks.
 *
 * Each iteration of a `for` header and each entry of a loop body block gets a
 * fresh box for captured lexical bindings, while `var` keeps its single
 * function-scoped binding. Before this, chalk built every style from the last
 * loop entry (its `styles` table is created in a `for...of` whose getters close
 * over the entry).
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("loop and block bindings", (harness) => {
  const { runProgram } = harness;

  it("gives captured let/const in a for header their own iteration", () => {
    const source = `
      const fns: (() => number)[] = [];
      for (let i = 0; i < 3; i++) fns.push(() => i);
      console.log(fns.map((f) => f()).join(","));
      const writes: (() => number)[] = [];
      for (let i = 0; i < 3; i++) { const j = i * 10; writes.push(() => i + j); }
      console.log(writes.map((f) => f()).join(","));
    `;
    expect(runProgram(source)).toBe("0,1,2\n0,11,22");
  });

  it("gives captured for...of bindings and body constants their own iteration", () => {
    const source = `
      const pairs: (() => string)[] = [];
      for (const [key, value] of Object.entries({ a: 1, b: 2 })) {
        const label = key + ":" + value;
        pairs.push(() => label);
      }
      console.log(pairs.map((f) => f()).join(","));
      const entries: (() => string)[] = [];
      for (const entry of Object.keys({ x: 1, y: 2 })) {
        entries.push(() => entry);
      }
      console.log(entries.map((f) => f()).join(","));
    `;
    expect(runProgram(source)).toBe("a:1,b:2\nx,y");
  });

  it("refreshes a captured block binding on each while iteration", () => {
    const source = `
      const fns: (() => number)[] = [];
      let i = 0;
      while (i < 3) {
        const copy = i;
        fns.push(() => copy);
        i++;
      }
      console.log(fns.map((f) => f()).join(","));
    `;
    expect(runProgram(source)).toBe("0,1,2");
  });

  it("keeps var function-scoped, so its closures see the final value", () => {
    const source = `
      const fns: (() => number)[] = [];
      for (var i = 0; i < 3; i++) fns.push(() => i);
      console.log(fns.map((f) => f()).join(","));
    `;
    expect(runProgram(source)).toBe("3,3,3");
  });
});
