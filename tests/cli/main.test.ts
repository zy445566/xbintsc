import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { run, type CliIo } from "../../src/cli/main.js";
import { hasClang } from "../helpers.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-cli-"));
  directories.push(directory);
  return directory;
}

/** Run `fn` inside a temp working directory so build caches never touch the repo. */
function withWorkingDirectory<T>(fn: (directory: string) => T): T {
  const created = mkdtempSync(join(tmpdir(), "xbintsc-cli-cwd-"));
  const previous = process.cwd();
  process.chdir(created);
  const directory = process.cwd();
  try {
    return fn(directory);
  } finally {
    process.chdir(previous);
    rmSync(created, { recursive: true, force: true });
  }
}

function capture(): { io: CliIo; out: string[]; err: string[] } {
  const out: string[] = [];
  const err: string[] = [];
  return { io: { stdout: (text) => out.push(text), stderr: (text) => err.push(text) }, out, err };
}

function writeProgram(source = "console.log(1 + 1);"): string {
  const directory = temporaryDirectory();
  const entry = join(directory, "program.ts");
  writeFileSync(entry, source);
  return entry;
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

  it("prints help when --help is passed with a command", () => {
    const { io, out } = capture();
    expect(run(["build", "--help"], io)).toBe(0);
    expect(out.join("")).toContain("Usage:");
  });

  it("prints the version", () => {
    const { io, out } = capture();
    expect(run(["version"], io)).toBe(0);
    expect(out.join("")).toMatch(/xbintsc \d+\.\d+\.\d+/);
  });

  it("honours a --version flag even without a known command", () => {
    const { io, out } = capture();
    expect(run(["whatever", "--version"], io)).toBe(0);
    expect(out.join("")).toMatch(/xbintsc \d+\.\d+\.\d+/);
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
    expect(run(["run"], io)).toBe(1);
    expect(err.join("")).toContain("requires a source file");
  });

  it("requires a file for emit", () => {
    const { io, err } = capture();
    expect(run(["emit"], io)).toBe(1);
    expect(err.join("")).toContain("requires a source file");
  });

  it("emits LLVM IR for a source file", () => {
    const entry = writeProgram();
    const { io, out } = capture();
    expect(run(["emit", entry], io)).toBe(0);
    const ir = out.join("");
    expect(ir).toContain("define i32 @main");
    expect(ir).toContain("@xt_add");
  });

  it("surfaces parse errors from emit", () => {
    const entry = writeProgram("const = ;");
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    expect(err.join("")).toContain("error TS");
  });

  it("parses inline, separate and short option forms", () => {
    const entry = writeProgram();
    const forms = [
      ["emit", entry, "--emit=ir"],
      ["emit", entry, "--emit", "ir"],
      ["emit", entry, "--output"],
      ["emit", entry, "--emit", "--verbose"],
      ["emit", entry, "-O0"],
      ["emit", entry, "-O"],
      ["emit", entry, "-O3"],
      ["emit", entry, "-o", "out"],
      ["emit", entry, "-v"],
      ["emit", entry, "--", "-not-a-flag"],
    ];
    for (const argv of forms) {
      const { io, err } = capture();
      expect(run(argv, io), `argv: ${argv.join(" ")}`).toBe(0);
      expect(err.join("")).toBe("");
    }
  });

  it("rejects unknown extensions", () => {
    const entry = writeProgram("console.log(1);");
    const { io } = capture();
    expect(() => run(["emit", entry, "--ext", "nope"], io)).toThrow(/Unknown extension/);
  });

  it("accepts the node extension", () => {
    const entry = writeProgram("console.log(1);");
    const { io } = capture();
    expect(run(["emit", entry, "--ext", "node"], io)).toBe(0);
  });

  it("points a missing node import at --ext node", () => {
    const entry = writeProgram('import { readFileSync } from "node:fs";\nreadFileSync("x");');
    const { io, err } = capture();
    expect(run(["emit", entry], io)).toBe(1);
    const output = err.join("");
    expect(output).toContain(
      "module 'node:fs' is provided by the 'node' extension; pass --ext node",
    );
    // The actionable hint replaces the confusing secondary error.
    expect(output).not.toContain("cannot be used as a value");
  });

  it("accepts the import once --ext node is passed", () => {
    const entry = writeProgram('import { readFileSync } from "node:fs";\nreadFileSync("x");');
    const { io, err } = capture();
    expect(run(["emit", entry, "--ext", "node"], io)).toBe(0);
    expect(err.join("")).toBe("");
  });

  it("registers a native extension from a manifest", () => {
    const entry = writeProgram("console.log(1);");
    const directory = temporaryDirectory();
    writeFileSync(join(directory, "libfake.a"), "fake archive");
    const manifest = join(directory, "ext.manifest.json");
    writeFileSync(
      manifest,
      JSON.stringify({ name: "fake", objects: ["libfake.a"], builtins: { fake: { symbol: "fake_impl" } } }),
    );
    const { io } = capture();
    expect(run(["emit", entry, "--ext-native", manifest], io)).toBe(0);
  });

  it("reports a missing native extension manifest", () => {
    const entry = writeProgram("console.log(1);");
    const { io } = capture();
    const missing = join(temporaryDirectory(), "nope.json");
    expect(() => run(["emit", entry, "--ext-native", missing], io)).toThrow(
      /Unable to read native extension manifest/,
    );
  });

  it("reports doctor information", () => {
    const { io, out } = capture();
    expect(run(["doctor"], io)).toBe(0);
    const text = out.join("");
    expect(text).toContain("platform");
    expect(text).toContain("toolchain");
    expect(text).toContain("runtime");
  });

  it("builds an IR artifact and then serves it from the cache", () => {
    const entry = writeProgram();
    withWorkingDirectory((directory) => {
      const outDir = join(directory, "out");
      const first = capture();
      expect(run(["build", entry, "--emit", "ir", "--out", outDir], first.io)).toBe(0);
      expect(first.out.join("")).toContain("wrote");

      const second = capture();
      expect(run(["build", entry, "--emit", "ir", "--out", outDir], second.io)).toBe(0);
      expect(second.out.join("")).toContain("(cached)");
    });
  });

  it("surfaces build errors", () => {
    const entry = writeProgram("const = ;");
    withWorkingDirectory(() => {
      const { io, err } = capture();
      expect(run(["build", entry], io)).toBe(1);
      expect(err.join("")).toContain("error TS");
    });
  });

  it("rejects `run` with a non-executable emit target", () => {
    const entry = writeProgram();
    withWorkingDirectory((directory) => {
      const { io, err } = capture();
      expect(run(["run", entry, "--emit", "ir", "--out", join(directory, "out")], io)).toBe(1);
      expect(err.join("")).toContain("run requires --emit exe");
    });
  });

  it.skipIf(!hasClang())("compiles and runs a program end to end", () => {
    const entry = writeProgram("console.log(2 + 3);");
    withWorkingDirectory((directory) => {
      const { io } = capture();
      expect(run(["run", entry, "--out", join(directory, "out")], io)).toBe(0);
    });
  }, 120000);
});
