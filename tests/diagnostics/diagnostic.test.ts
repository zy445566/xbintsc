import { describe, expect, it } from "vitest";
import {
  DiagnosticBag,
  DiagnosticCode,
  DiagnosticError,
  formatDiagnostic,
  formatDiagnostics,
  type Diagnostic,
} from "../../src/diagnostics/diagnostic.js";
import { SourceFile } from "../../src/diagnostics/source.js";

describe("DiagnosticBag", () => {
  it("records errors and warnings with optional locations", () => {
    const bag = new DiagnosticBag();
    bag.error(DiagnosticCode.UnexpectedToken, "boom", { start: 1, end: 3 }, "a.ts");
    bag.warning(DiagnosticCode.UnsupportedFeature, "careful");
    expect(bag.diagnostics).toHaveLength(2);
    expect(bag.diagnostics[0]).toMatchObject({
      category: "error",
      code: DiagnosticCode.UnexpectedToken,
      message: "boom",
      fileName: "a.ts",
      start: 1,
      end: 3,
    });
    expect(bag.diagnostics[1]).toMatchObject({ category: "warning", message: "careful" });
  });

  it("tracks whether any error has been reported", () => {
    const bag = new DiagnosticBag();
    expect(bag.hasErrors).toBe(false);
    bag.warning(DiagnosticCode.UnsupportedFeature, "just a warning");
    expect(bag.hasErrors).toBe(false);
    bag.error(DiagnosticCode.CodegenError, "real error");
    expect(bag.hasErrors).toBe(true);
  });

  it("marks and rolls back speculative diagnostics", () => {
    const bag = new DiagnosticBag();
    bag.error(DiagnosticCode.ExpectedToken, "first");
    const mark = bag.mark();
    bag.error(DiagnosticCode.ExpectedToken, "speculative");
    bag.warning(DiagnosticCode.ExpectedToken, "speculative warning");
    expect(bag.diagnostics).toHaveLength(3);
    bag.reset(mark);
    expect(bag.diagnostics).toHaveLength(1);
    expect(bag.diagnostics[0]!.message).toBe("first");
  });

  it("adds pre-built diagnostics individually and in bulk", () => {
    const first: Diagnostic = { category: "error", code: DiagnosticCode.TypeMismatch, message: "one" };
    const second: Diagnostic = { category: "info", code: DiagnosticCode.TypeMismatch, message: "two" };
    const bag = new DiagnosticBag();
    bag.add(first);
    bag.addAll([second]);
    expect(bag.diagnostics).toEqual([first, second]);
  });

  it("throws a DiagnosticError only when errors are present", () => {
    const clean = new DiagnosticBag();
    clean.warning(DiagnosticCode.UnsupportedFeature, "no throw");
    expect(() => clean.throwIfErrors()).not.toThrow();

    const dirty = new DiagnosticBag();
    dirty.error(DiagnosticCode.CodegenError, "kaboom");
    expect(() => dirty.throwIfErrors()).toThrow(DiagnosticError);
  });
});

describe("DiagnosticError", () => {
  it("collects a single diagnostic into an array", () => {
    const error = new DiagnosticError({ category: "error", code: DiagnosticCode.IOError, message: "io" });
    expect(error.name).toBe("DiagnosticError");
    expect(error.diagnostics).toHaveLength(1);
    expect(error.message).toContain("TS6002");
  });

  it("accepts multiple diagnostics and joins their text", () => {
    const error = new DiagnosticError([
      { category: "error", code: DiagnosticCode.IOError, message: "one" },
      { category: "error", code: DiagnosticCode.CodegenError, message: "two" },
    ]);
    expect(error.diagnostics).toHaveLength(2);
    expect(error.message.split("\n")).toHaveLength(2);
  });
});

describe("formatDiagnostic", () => {
  it("falls back to <unknown> when there is no file name", () => {
    const text = formatDiagnostic({ category: "error", code: DiagnosticCode.CodegenError, message: "oops" });
    expect(text).toBe("<unknown> - error TS5001: oops");
  });

  it("uses the diagnostic file name when no source is available", () => {
    const text = formatDiagnostic({
      category: "warning",
      code: DiagnosticCode.UnsupportedFeature,
      message: "hmm",
      fileName: "b.ts",
    });
    expect(text).toBe("b.ts - warning TS4005: hmm");
  });

  it("renders a source excerpt with a caret underline", () => {
    const source = new SourceFile("a.ts", "let a = 1;\nlet b = 2;");
    const text = formatDiagnostic(
      { category: "error", code: DiagnosticCode.CannotFindName, message: "no b", start: 15, end: 16 },
      source,
    );
    expect(text).toContain("a.ts:2:5 - error TS3002: no b");
    expect(text).toContain("let b = 2;");
    expect(text).toContain("\n    ^");
  });

  it("widens the caret to cover the diagnostic range", () => {
    const source = new SourceFile("a.ts", "abcdef");
    const text = formatDiagnostic(
      { category: "error", code: DiagnosticCode.CodegenError, message: "wide", start: 1, end: 4 },
      source,
    );
    expect(text).toContain("\n ^^^");
  });

  it("omits the excerpt when the source has no start offset", () => {
    const source = new SourceFile("a.ts", "abc");
    const text = formatDiagnostic(
      { category: "error", code: DiagnosticCode.CodegenError, message: "no pos", fileName: "a.ts" },
      source,
    );
    expect(text).toBe("a.ts - error TS5001: no pos");
  });
});

describe("formatDiagnostics", () => {
  it("looks up each source by file name", () => {
    const source = new SourceFile("a.ts", "let x = ;");
    const sources = new Map([["a.ts", source]]);
    const text = formatDiagnostics(
      [
        { category: "error", code: DiagnosticCode.ExpectedToken, message: "missing", fileName: "a.ts", start: 8 },
        { category: "info", code: DiagnosticCode.ExpectedToken, message: "other" },
      ],
      sources,
    );
    const lines = text.split("\n");
    expect(lines[0]).toContain("a.ts:1:9 - error TS2002: missing");
    expect(text).toContain("<unknown> - info TS2002: other");
  });
});
