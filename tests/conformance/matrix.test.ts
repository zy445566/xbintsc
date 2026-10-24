/**
 * Keeps `doc/support-matrix.md` derivable: if a case is added, removed or
 * renamed and the document is not regenerated, this test fails with the command
 * that fixes it.
 */

import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { MATRIX_FILES, renderSupportMatrix } from "../../scripts/support-matrix.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

describe("support matrix", () => {
  for (const language of ["en", "zh"] as const) {
    it(`matches the corpus (${language})`, () => {
      const committed = readFileSync(resolve(root, MATRIX_FILES[language]), "utf8");
      const expected = renderSupportMatrix(language);
      expect(
        committed,
        `${MATRIX_FILES[language]} is stale; run \`npm run docs:matrix\``,
      ).toBe(expected);
    });
  }
});
