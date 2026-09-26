/**
 * Shared helpers for the differential suites: register one case per program and
 * build programs that print canonical `JSON.stringify` output.
 */

import { it } from "vitest";
import type { E2EHarness } from "./harness.js";

export interface DifferentialKit {
  /** Register one differential case from a complete program source. */
  diff(name: string, source: string): void;
  /** Wrap a list of expressions so each is printed as a `JSON.stringify` line. */
  printAll(values: string[]): string;
}

export function makeDifferential(harness: E2EHarness): DifferentialKit {
  const diff = (name: string, source: string): void => {
    it(name, () => {
      harness.expectSameOutputAsNode(source, { name: `diff_${name.replace(/\W+/g, "_")}` });
    });
  };

  const printAll = (values: string[]): string =>
    `const values: unknown[] = [\n${values.map((v) => `  ${v},`).join("\n")}\n];\n` +
    `for (const v of values) console.log(JSON.stringify(v));\n`;

  return { diff, printAll };
}
