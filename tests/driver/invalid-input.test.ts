/**
 * Malformed source must produce diagnostics, never an uncaught exception.
 *
 * Code generation (and the binder inside it) assumes required AST children
 * exist; a parse error leaves them missing. The driver therefore has to stop
 * after a failed parse instead of lowering the tree, which previously crashed
 * with "Cannot read properties of undefined (reading 'text')" for inputs such
 * as `function () {}` or `class {`.
 */

import { describe, expect, it } from "vitest";
import { compileString } from "../../src/driver/compiler.js";

describe("compiler input robustness", () => {
  it("returns diagnostics instead of throwing for malformed source", () => {
    const malformed = [
      "function () {}",
      "class {",
      "const = ;",
      "let x = 1 +",
      "console.log(",
      "if (true) {",
    ];
    for (const source of malformed) {
      const result = compileString(source, "malformed.ts");
      expect(result.ir, source).toBe("");
      expect(
        result.diagnostics.some((diagnostic) => diagnostic.category === "error"),
        source,
      ).toBe(true);
    }
  });
});
