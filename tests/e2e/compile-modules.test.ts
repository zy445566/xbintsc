/**
 * End-to-end tests for the module bundler.
 */

import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end module bundling", (harness) => {
  const { runProgram } = harness;

  it("supports import and export across modules", () => {
    writeFileSync(
      join(harness.workdir, "math_dep.ts"),
      `
      export const PI = 3.14;
      export function add(a: number, b: number): number {
        return a + b;
      }
      export default function greet(name: string): string {
        return "hi " + name;
      }
      `,
    );
    const source = `
      import greet, { add, PI } from "./math_dep";
      console.log(add(2, 3), PI);
      console.log(greet("ada"));
    `;
    expect(runProgram(source)).toBe("5 3.14\nhi ada");
  });

  it("resolves `.js` specifiers to their TypeScript sources", () => {
    writeFileSync(join(harness.workdir, "esm_dep.ts"), `export const value = 42;`);
    const source = `
      import { value } from "./esm_dep.js";
      console.log(value);
    `;
    expect(runProgram(source)).toBe("42");
  });

  it("bundles JavaScript modules through extension-less imports", () => {
    const source = `
      import { add } from "./js_dep";
      console.log(add(2, 3));
    `;
    expect(
      runProgram(source, {
        files: {
          "js_dep.js": 'export { add } from "./js_math";',
          "js_math.js": "export function add(a, b) { return a + b; }",
        },
      }),
    ).toBe("5");
  });
});
