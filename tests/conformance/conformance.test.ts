/**
 * Runs the conformance corpus: every supported case must compile **and** match
 * Node's stdout byte for byte; every unsupported case must fail with the
 * documented diagnostic code. With the checker on by default, the supported
 * cases double as evidence that legitimate code is not rejected.
 */

import { describe, expect, it } from "vitest";
import { compileString } from "../../src/driver/frontend.js";
import { describeE2E } from "../e2e/harness.js";
import { SUPPORT_AREAS, SUPPORTED_CASES, UNSUPPORTED_CASES } from "./corpus.js";

describeE2E("conformance corpus (differential against Node)", (harness) => {
  for (const testCase of SUPPORTED_CASES) {
    it(`${testCase.area}: ${testCase.name}`, () => {
      harness.expectSameOutputAsNode(testCase.source, {
        name: `conformance_${testCase.name.replace(/\W+/g, "_")}`,
        ...(testCase.files ? { files: testCase.files } : {}),
      });
    });
  }
});

describe("conformance corpus (boundary)", () => {
  for (const testCase of UNSUPPORTED_CASES) {
    it(`${testCase.area}: ${testCase.name}`, () => {
      const { diagnostics } = compileString(testCase.source, `${testCase.name}.ts`);
      const codes = diagnostics.map((diagnostic) => diagnostic.code);
      expect(codes, `${testCase.name}: ${JSON.stringify(diagnostics)}`).toContain(testCase.code);
    });
  }
});

describe("conformance corpus (catalogue)", () => {
  it("names every case uniquely", () => {
    const names = [...SUPPORTED_CASES, ...UNSUPPORTED_CASES].map((testCase) => testCase.name);
    expect(new Set(names).size).toBe(names.length);
  });

  it("covers every documented area", () => {
    const covered = new Set([...SUPPORTED_CASES, ...UNSUPPORTED_CASES].map((testCase) => testCase.area));
    expect([...covered].sort()).toEqual(SUPPORT_AREAS.map((area) => area.id).sort());
  });
});
