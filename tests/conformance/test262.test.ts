/**
 * Runs the vendored test262 cases.
 *
 * Supported cases are unmodified test262 files: xbintsc compiles them together
 * with a compatible assertion harness and the binary must exit 0 (async cases
 * must also report completion). Unsupported cases are valid JavaScript that
 * xbintsc still rejects; they must fail with the pinned diagnostic code, so
 * implementing the construct makes the case fail and forces the catalogue (and
 * the support matrix) to move it.
 */

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { compileString } from "../../src/driver/frontend.js";
import { describeE2E } from "../e2e/harness.js";
import { SUPPORT_AREAS, SUPPORTED_CASES, UNSUPPORTED_CASES } from "./test262/manifest.js";

const corpusRoot = resolve(dirname(fileURLToPath(import.meta.url)), "test262");
const HARNESS = readFileSync(join(corpusRoot, "harness", "assert.js"), "utf8");
const ASYNC_COMPLETE = "Test262:AsyncTestComplete";

function caseSource(path: string): string {
  return readFileSync(join(corpusRoot, "cases", path), "utf8");
}

/** Read `flags:` from the test262 frontmatter. */
function flagsOf(source: string): readonly string[] {
  const block = /\/\*---([\s\S]*?)---\*\//.exec(source)?.[1] ?? "";
  const value = /flags:\s*\[([^\]]*)\]/.exec(block)?.[1] ?? "";
  return value
    .split(",")
    .map((flag) => flag.trim())
    .filter((flag) => flag.length > 0);
}

function entryName(path: string): string {
  return `t262_${path.replace(/\W+/g, "_")}`;
}

describeE2E("test262 conformance corpus", (harness) => {
  for (const testCase of SUPPORTED_CASES) {
    it(`${testCase.area}: ${testCase.path}`, () => {
      const original = caseSource(testCase.path);
      const flags = flagsOf(original);
      const strict = flags.includes("onlyStrict") || flags.includes("alwaysStrict");
      const source = `${strict ? '"use strict";\n' : ""}${HARNESS}\n${original}`;
      const result = harness.runProgramFull(source, {
        name: entryName(testCase.path),
        timeout: 30_000,
      });
      expect(result.status, result.stderr).toBe(0);
      if (flags.includes("async")) {
        expect(result.stdout, "async test did not call $DONE").toContain(ASYNC_COMPLETE);
      }
    });
  }
});

describe("test262 unsupported corpus (boundary)", () => {
  for (const testCase of UNSUPPORTED_CASES) {
    it(`${testCase.area}: ${testCase.path}`, () => {
      const source = `${HARNESS}\n${caseSource(testCase.path)}`;
      const { diagnostics } = compileString(source, testCase.path);
      const codes = diagnostics.map((diagnostic) => diagnostic.code);
      expect(codes, JSON.stringify(diagnostics)).toContain(testCase.code);
    });
  }
});

describe("test262 corpus (catalogue)", () => {
  it("names every case uniquely", () => {
    const paths = [...SUPPORTED_CASES, ...UNSUPPORTED_CASES].map((testCase) => testCase.path);
    expect(new Set(paths).size).toBe(paths.length);
  });

  it("covers every documented area", () => {
    const covered = new Set([...SUPPORTED_CASES, ...UNSUPPORTED_CASES].map((testCase) => testCase.area));
    expect([...covered].sort()).toEqual(SUPPORT_AREAS.map((area) => area.id).sort());
  });
});
