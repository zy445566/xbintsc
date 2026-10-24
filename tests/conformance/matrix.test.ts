/**
 * Keeps `doc/support-matrix.md` derivable: if a case is added, removed or
 * renamed and the document is not regenerated, this test fails with the command
 * that fixes it.
 */

import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { MATRIX_FILES, renderSupportMatrix } from "../../scripts/support-matrix.js";
import { readTextFile } from "../platform.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

describe("support matrix", () => {
  for (const language of ["en", "zh"] as const) {
    it(`matches the corpus (${language})`, () => {
      // The renderer always produces LF; read through the helper so a Windows
      // checkout with CRLF cannot fail the comparison.
      const committed = readTextFile(resolve(root, MATRIX_FILES[language]));
      const expected = renderSupportMatrix(language);
      expect(
        committed,
        `${MATRIX_FILES[language]} is stale; run \`npm run docs:matrix\``,
      ).toBe(expected);
    });
  }
});
