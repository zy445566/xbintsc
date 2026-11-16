/**
 * `createRequire` in a compiled binary.
 *
 * A binary has no module loader for arbitrary files, but the modules linked
 * into it are known: the code generator registers a table of their namespace
 * objects, so the `require` returned by `createRequire` answers `fs`, `path`,
 * … instead of throwing for every specifier.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (createRequire)", (harness) => {
  const { runProgram } = harness;

  it("resolves a specifier against the modules linked into the binary", () => {
    const source = `
      import { createRequire } from "node:module";
      const require = createRequire(import.meta.url);
      const fs = require("node:fs") as typeof import("fs");
      const path = require("path") as typeof import("path");
      console.log(typeof fs.readFileSync, typeof path.join);
      try {
        require("package-that-is-not-linked");
      } catch (error) {
        console.log(String(error).includes("not linked"));
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("function function\ntrue");
  });
});
