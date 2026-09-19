import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { build, compileString } from "../../src/driver/compiler.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbtsc-build-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("compileString", () => {
  it("turns source into LLVM IR", () => {
    const { ir, diagnostics } = compileString("console.log(1 + 2);", "inline.ts");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("define i32 @main");
    expect(ir).toContain("@xt_add");
  });

  it("reports parse errors without producing a broken module", () => {
    const { diagnostics } = compileString("const = ;", "bad.ts");
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });
});

describe("build", () => {
  it("emits an IR file and skips work on the second run", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(21 * 2);");
    const outDir = join(directory, "out");

    const first = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache") });
    expect(first.cached).toBe(false);
    expect(first.outputPath.endsWith(".ll")).toBe(true);
    expect(existsSync(first.outputPath)).toBe(true);
    expect(readFileSync(first.outputPath, "utf8")).toContain("@xt_mul");

    const second = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache") });
    expect(second.cached).toBe(true);

    const forced = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache"), force: true });
    expect(forced.cached).toBe(false);
  });

  it("returns diagnostics for invalid programs", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "bad.ts");
    writeFileSync(entry, "const = ;");
    const result = build(entry, { emit: "ir", outDir: join(directory, "out"), cacheDir: join(directory, ".cache") });
    expect(result.diagnostics.some((d) => d.category === "error")).toBe(true);
  });

  it("drives clang through the toolchain to link an executable", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const calls: { command: string; args: string[] }[] = [];
    const runner: Runner = {
      run(command, args): CommandResult {
        calls.push({ command, args: [...args] });
        return { status: 0, stdout: "", stderr: "" };
      },
    };

    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
    });

    expect(result.diagnostics).toHaveLength(0);
    // clang was used for the runtime object, the module object and the link.
    const compileCalls = calls.filter((call) => call.args.includes("-c"));
    expect(compileCalls.length).toBeGreaterThanOrEqual(2);
    const linkCall = calls.at(-1)!;
    expect(linkCall.args).toContain("-o");
    expect(linkCall.args).toContain(result.outputPath);
    if (process.platform !== "win32") expect(linkCall.args).toContain("-lm");
  });
});
