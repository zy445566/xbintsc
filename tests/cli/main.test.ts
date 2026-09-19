import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { run, type CliIo } from "../../src/cli/main.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xtsc-cli-"));
  directories.push(directory);
  return directory;
}

function capture(): { io: CliIo; out: string[]; err: string[] } {
  const out: string[] = [];
  const err: string[] = [];
  return { io: { stdout: (text) => out.push(text), stderr: (text) => err.push(text) }, out, err };
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("cli", () => {
  it("prints help with no arguments", () => {
    const { io, out } = capture();
    expect(run([], io)).toBe(0);
    expect(out.join("")).toContain("Usage:");
  });

  it("prints the version", () => {
    const { io, out } = capture();
    expect(run(["version"], io)).toBe(0);
    expect(out.join("")).toMatch(/xtsc \d+\.\d+\.\d+/);
  });

  it("rejects unknown commands", () => {
    const { io, err } = capture();
    expect(run(["frobnicate"], io)).toBe(1);
    expect(err.join("")).toContain("unknown command");
  });

  it("requires a file for build and run", () => {
    const { io, err } = capture();
    expect(run(["build"], io)).toBe(1);
    expect(err.join("")).toContain("requires a source file");
  });

  it("emits LLVM IR for a source file", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "program.ts");
    writeFileSync(entry, "console.log(1 + 1);");
    const { io, out } = capture();
    expect(run(["emit", entry], io)).toBe(0);
    const ir = out.join("");
    expect(ir).toContain("define i32 @main");
    expect(ir).toContain("@xt_add");
  });

  it("surfaces parse errors from emit", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "bad.ts");
    writeFileSync(entry, "const = ;");
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    expect(err.join("")).toContain("error TS");
  });

  it("rejects unknown extensions", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "program.ts");
    writeFileSync(entry, "console.log(1);");
    const { io } = capture();
    expect(() => run(["emit", entry, "--ext", "nope"], io)).toThrow(/Unknown extension/);
  });
});
