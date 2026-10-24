/**
 * `xbintsc check`: run the front end (parse, bundle, lower for diagnostics)
 * without invoking clang or writing an output file. `--json` makes the
 * diagnostics machine readable for editors and CI.
 */

import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { run, type CliIo } from "../../src/cli/main.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-check-"));
  directories.push(directory);
  return directory;
}

function capture(): { io: CliIo; out: string[]; err: string[] } {
  const out: string[] = [];
  const err: string[] = [];
  return { io: { stdout: (text) => out.push(text), stderr: (text) => err.push(text) }, out, err };
}

function writeProgram(source: string, files: Record<string, string> = {}): string {
  const directory = temporaryDirectory();
  for (const [name, contents] of Object.entries(files)) writeFileSync(join(directory, name), contents);
  const entry = join(directory, "app.ts");
  writeFileSync(entry, source);
  return entry;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("cli check", () => {
  it("accepts a valid program without writing output", () => {
    const entry = writeProgram("const answer: number = 42;\nconsole.log(answer);");
    const { io, out, err } = capture();
    expect(run(["check", entry], io)).toBe(0);
    expect(out.join("")).toBe("");
    expect(err.join("")).toBe("");
    expect(existsSync(join(dirname(entry), "build"))).toBe(false);
  });

  it("checks imported modules", () => {
    const entry = writeProgram('import { add } from "./math.ts";\nconsole.log(add(1, 2));', {
      "math.ts": "export function add(a: number, b: number): number { return a + b; }",
    });
    const { io } = capture();
    expect(run(["check", entry], io)).toBe(0);
  });

  it("runs the semantic checker by default", () => {
    for (const source of [
      "let value = 1; let value = 2; console.log(value);", // TS3001
      "function f(a: number) { return a; } f(1, 2);", // TS4004
      "console.log(missing);", // TS3002
    ]) {
      const entry = writeProgram(source);
      const { io, err } = capture();
      expect(run(["check", entry], io), source).toBe(1);
      expect(err.join(""), source).toContain("error TS");
    }
  });

  it("lets --no-check restore the permissive pipeline", () => {
    const entry = writeProgram("let value = 1; let value = 2; console.log(value);");
    const { io } = capture();
    expect(run(["check", entry, "--no-check"], io)).toBe(0);
  });

  it("reports syntax and unsupported-feature diagnostics", () => {
    for (const source of ["const value = ;", "namespace N { export const x = 1; }"]) {
      const entry = writeProgram(source);
      const { io, err } = capture();
      expect(run(["check", entry], io), source).toBe(1);
      expect(err.join(""), source).toContain("error TS");
    }
  });

  it("prints a JSON diagnostics array", () => {
    const entry = writeProgram("const value: number = ;");
    const { io, out } = capture();
    expect(run(["check", entry, "--json"], io)).toBe(1);
    const parsed = JSON.parse(out.join("")) as Array<{ code: number; message: string }>;
    expect(Array.isArray(parsed)).toBe(true);
    expect(parsed[0]!.code).toBeGreaterThan(0);
    expect(parsed[0]!.message).toContain("Unexpected token");
  });
});
