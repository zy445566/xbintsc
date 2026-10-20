/**
 * `Error.stack` and the shadow call stack that backs it.
 *
 * The frames come from codegen: every non-generator function pushes a frame
 * (display name, source file, line) on entry and pops it before returning.
 * Assertions check structure rather than exact formatting so the tests keep
 * working across platforms (path separators) and small formatting changes.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("error stacks", (harness) => {
  it("captures a stack with function names, files and lines", () => {
    const source = `
      function inner(): never { throw new Error("boom"); }
      function middle(): void { inner(); }
      try {
        middle();
      } catch (e) {
        const stack = (e as Error).stack;
        console.log(stack.split("\\n")[0]);
        console.log(stack.includes("at inner"));
        console.log(stack.includes("at middle"));
        console.log(stack.includes("at module"));
        console.log(stack.includes("error_stack"));
      }
    `;
    expect(harness.runProgram(source, { name: "error_stack" })).toBe("Error: boom\ntrue\ntrue\ntrue\ntrue");
  });

  it("keeps stack out of enumeration and JSON and reports it as a string", () => {
    const source = `
      const plain = new Error("plain");
      console.log(typeof plain.stack);
      console.log(Object.keys(plain).includes("stack"));
      console.log(JSON.stringify(plain).includes("stack"));
      console.log(plain.stack.split("\\n")[0]);
      console.log(plain.stack.includes("at module"));
    `;
    expect(harness.runProgram(source)).toBe("string\nfalse\nfalse\nError: plain\ntrue");
  });

  it("captures stacks for Error subclasses", () => {
    const source = `
      class MyError extends Error {
        constructor(message: string) { super(message); this.name = "MyError"; }
      }
      try {
        throw new MyError("custom");
      } catch (e) {
        const stack = (e as Error).stack;
        console.log(e instanceof MyError, (e as Error).name);
        console.log(stack.split("\\n")[0]);
        console.log(stack.includes("at new MyError"));
      }
    `;
    expect(harness.runProgram(source)).toBe("true MyError\nError: custom\ntrue");
  });

  it("prints the stack for an uncaught error", () => {
    const result = harness.runProgramFull(`
      function inner(): never { throw new Error("boom"); }
      inner();
    `);
    expect(result.status).not.toBe(0);
    expect(result.stderr).toContain("Uncaught Error: boom");
    expect(result.stderr).toContain("at inner");
    expect(result.stderr).toContain("at module");
    expect(result.stderr).toContain(":1");
  });

  it("names bundled functions without the bundler's module prefix", () => {
    const output = harness.runProgram(
      `
      import { fail } from "./failure.ts";
      try { fail(); } catch (e) { console.log((e as Error).stack); }
      `,
      {
        name: "bundled_stack",
        files: {
          "failure.ts": 'export function fail(): never { throw new Error("from helper"); }\n',
        },
      },
    );
    expect(output).toContain("Error: from helper");
    expect(output).toContain("at fail (");
    expect(output).toContain("failure.ts:");
    expect(output).not.toContain("_fail (");
  });

  it("does not leak frames from a call unwound by catch", () => {
    const source = `
      function inner(): never { throw new Error("boom"); }
      try { inner(); } catch (e) { console.log((e as Error).message); }
      const after = new Error("after");
      console.log(after.stack.includes("inner"));
      console.log(after.stack.includes("middle"));
      console.log(after.stack.includes("at module"));
    `;
    expect(harness.runProgram(source)).toBe("boom\nfalse\nfalse\ntrue");
  });
});
