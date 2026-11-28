/**
 * A failure inside a bundle's module body names that module.
 *
 * Every module of a bundle is emitted into one generated function, so its stack
 * frames carry the *entry* file's coordinates (`cli.ts:653` for a module in
 * `some-package/dist/index.js`). Codegen records the module that is
 * initialising, and the runtime prints it above the frames.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (module attribution)", (harness) => {
  const { runProgramFull } = harness;

  it("names the module whose top-level code threw", () => {
    const result = runProgramFull('import { broken } from "./bad";\nconsole.log(broken);', {
      files: {
        "bad.ts": "export const broken = (undefined as unknown as { thing: number }).thing;",
      },
    });
    expect(result.stderr).toContain("reading 'thing'");
    expect(result.stderr).toContain("module: ");
    expect(result.stderr).toContain("bad.ts");
  });

  it("does not carry the last module into an error thrown after startup", () => {
    const result = runProgramFull(
      'import "./good";\nsetTimeout(() => { (undefined as unknown as { x: number }).x; }, 0);',
      { files: { "good.ts": "export const value = 1;" } },
    );
    expect(result.stderr).toContain("Cannot read properties of undefined");
    expect(result.stderr).not.toContain("module: ");
  });

  // The diagnostic itself used to throw first: formatting the symbol key with
  // the implicit `xt_to_string` raised "Cannot convert a Symbol value to a
  // string" instead of the nullish-property TypeError.
  it("formats a symbol property key in a nullish read error", () => {
    const source = `
      try {
        (undefined as unknown as Record<symbol, number>)[Symbol.iterator];
      } catch (error) {
        console.log(String(error));
      }
    `;
    expect(harness.runProgram(source)).toBe(
      "TypeError: Cannot read properties of undefined (reading 'Symbol(Symbol.iterator)')",
    );
  });
});
