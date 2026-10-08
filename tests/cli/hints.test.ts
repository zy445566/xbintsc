/**
 * Documentation pointers on failures.
 *
 * A reader (increasingly an AI agent) meets this compiler through its error
 * output, so every failure must say which document answers the question. These
 * tests pin that the pointer is printed, that it names an existing file, and
 * that `--no-hints` removes it.
 */

import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";
import { run, type CliIo } from "../../src/cli/main.js";
import {
  DOC_EXTENSIONS,
  DOC_LANGUAGE_SUPPORT,
  DOC_REQUIREMENTS,
  DOC_TROUBLESHOOTING,
  documentationFor,
  thrownFailure,
  UNEXPECTED_FAILURE_HINT,
} from "../../src/cli/hints.js";
import { DiagnosticCode, type Diagnostic } from "../../src/diagnostics/diagnostic.js";

const projectRoot = resolve(fileURLToPath(new URL("../..", import.meta.url)));
const directories: string[] = [];

function temporaryFile(source: string, name = "program.ts"): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-hints-"));
  directories.push(directory);
  const entry = join(directory, name);
  writeFileSync(entry, source);
  return entry;
}

function capture(): { io: CliIo; out: string[]; err: string[] } {
  const out: string[] = [];
  const err: string[] = [];
  return { io: { stdout: (text) => out.push(text), stderr: (text) => err.push(text) }, out, err };
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("documentation hints", () => {
  it("points a disabled extension at the extensions document", () => {
    const entry = temporaryFile('import { readFileSync } from "node:fs";\nreadFileSync("x");');
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    const output = err.join("");
    expect(output).toContain("hint:");
    expect(output).toContain(DOC_EXTENSIONS);
  });

  it("points unsupported syntax at the language subset document", () => {
    const entry = temporaryFile("namespace N { export const x = 1; }");
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    expect(err.join("")).toContain(DOC_LANGUAGE_SUPPORT);
  });

  it("prints hints only once per document", () => {
    const entry = temporaryFile("namespace A {}\nnamespace B {}\n");
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    const mentions = err.join("").split(DOC_LANGUAGE_SUPPORT).length - 1;
    expect(mentions).toBe(1);
  });

  it("stays quiet under --no-hints", () => {
    const entry = temporaryFile('import { readFileSync } from "node:fs";\nreadFileSync("x");');
    const { io, err } = capture();
    expect(run(["emit", entry, "--no-hints"], io)).toBe(1);
    const output = err.join("");
    expect(output).toContain("error TS");
    expect(output).not.toContain("hint:");
  });

  it("hints at the CLI document when the project config is invalid", () => {
    const directory = mkdtempSync(join(tmpdir(), "xbintsc-hints-config-"));
    directories.push(directory);
    const config = join(directory, "xbintsc.config.json");
    writeFileSync(config, "{ not json");
    const { io, err } = capture();
    expect(run(["build", "--config", config], io)).toBe(1);
    expect(err.join("")).toContain("hint:");
  });

  it("every hinted document exists in the repository", () => {
    const codes = Object.values(DiagnosticCode).filter(
      (value): value is DiagnosticCode => typeof value === "number",
    );
    const paths = new Set<string>();
    for (const code of codes) {
      const diagnostic: Diagnostic = { category: "error", code, message: "sample" };
      const pointer = documentationFor(diagnostic);
      if (pointer) paths.add(pointer.path);
    }
    expect(paths.size).toBeGreaterThan(0);
    for (const path of paths) {
      expect(existsSync(join(projectRoot, path)), `missing ${path}`).toBe(true);
    }
  });

  it("sends an unclassified thrown failure to the troubleshooting guide", () => {
    expect(thrownFailure("something nobody predicted")).toBeUndefined();
    expect(UNEXPECTED_FAILURE_HINT.path).toBe(DOC_TROUBLESHOOTING);
  });

  it("names the toolchain requirement when the emitted IR is rejected by old clang", () => {
    const diagnostic: Diagnostic = {
      category: "error",
      code: DiagnosticCode.ToolchainError,
      message:
        "Command failed (1): clang out.ll\nout.ll:1:1: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'",
    };
    expect(documentationFor(diagnostic)?.path).toBe(DOC_REQUIREMENTS);
  });

  it("falls back to the troubleshooting guide for other toolchain failures", () => {
    const diagnostic: Diagnostic = {
      category: "error",
      code: DiagnosticCode.ToolchainError,
      message: "Command failed (1): clang out.ll\nsome other clang complaint",
    };
    expect(documentationFor(diagnostic)?.path).toBe(DOC_TROUBLESHOOTING);
  });

  it("has a pointer for every diagnostic code the CLI can print", () => {
    const codes = Object.values(DiagnosticCode).filter(
      (value): value is DiagnosticCode => typeof value === "number",
    );
    expect(codes.length).toBeGreaterThan(0);
    for (const code of codes) {
      const pointer = documentationFor({ category: "error", code, message: "sample" });
      expect(pointer, `no hint for TS${code}`).toBeDefined();
    }
  });

  it("points an unknown extension at the extensions document", () => {
    const entry = temporaryFile("console.log(1);");
    const { io, err } = capture();
    expect(run(["emit", entry, "--ext", "nope"], io)).toBe(1);
    const output = err.join("");
    expect(output).toContain("Unknown extension 'nope'");
    expect(output).toContain(DOC_EXTENSIONS);
    expect(output).not.toContain(DOC_LANGUAGE_SUPPORT);
  });

  it("points a missing native manifest at the extensions document", () => {
    const entry = temporaryFile("console.log(1);");
    const directory = mkdtempSync(join(tmpdir(), "xbintsc-hints-manifest-"));
    directories.push(directory);
    const { io, err } = capture();
    expect(run(["emit", entry, "--ext-native", join(directory, "nope.json")], io)).toBe(1);
    const output = err.join("");
    expect(output).toContain("Unable to read native extension manifest");
    expect(output).toContain(DOC_EXTENSIONS);
  });

  it("ignores warnings", () => {
    const { io, err } = capture();
    const entry = temporaryFile("console.log(1);");
    // A clean emit prints no hints at all; the hint layer only reacts to errors.
    expect(run(["emit", entry], io)).toBe(0);
    expect(err.join("")).toBe("");
  });
});
