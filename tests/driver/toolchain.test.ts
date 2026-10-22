import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import {
  compileC,
  compileIr,
  findClang,
  link,
  mergeProfileData,
  MINIMUM_CLANG_MAJOR,
  parseClangVersion,
  probeClang,
  ToolchainError,
  type CommandResult,
  type Runner,
} from "../../src/driver/toolchain.js";

interface Recorded {
  command: string;
  args: readonly string[];
}

function recordingRunner(status = 0): { runner: Runner; calls: Recorded[] } {
  const calls: Recorded[] = [];
  const runner: Runner = {
    run(command, args): CommandResult {
      calls.push({ command, args: [...args] });
      return { status, stdout: "", stderr: status === 0 ? "" : "boom" };
    },
  };
  return { runner, calls };
}

describe("toolchain", () => {
  it("parses clang version strings from the common spellings", () => {
    expect(parseClangVersion("clang version 19.1.5 (https://github.com/llvm/llvm-project.git)")).toEqual({
      major: 19,
      minor: 1,
      patch: 5,
    });
    expect(parseClangVersion("Apple clang version 15.0.0 (clang-1500.3.9.4)")).toEqual({
      major: 15,
      minor: 0,
      patch: 0,
    });
    expect(parseClangVersion("Ubuntu clang version 18.1.3 (1ubuntu1)")).toEqual({
      major: 18,
      minor: 1,
      patch: 3,
    });
    expect(parseClangVersion("cc (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0")).toBeUndefined();
    expect(parseClangVersion("")).toBeUndefined();
  });

  it("requires clang 16 or newer", () => {
    expect(MINIMUM_CLANG_MAJOR).toBe(16);
  });

  it("reports the version and first line of a probe", () => {
    const runner: Runner = {
      run: () => ({ status: 0, stdout: "clang version 19.1.5\nTarget: x86_64-pc-linux-gnu", stderr: "" }),
    };
    expect(probeClang(runner, "clang")).toEqual({
      command: "clang",
      responds: true,
      version: { major: 19, minor: 1, patch: 5 },
      target: "x86_64-pc-linux-gnu",
      summary: "clang version 19.1.5",
    });
  });

  it("honours xbintsc_CLANG when locating a compiler", () => {
    const { runner, calls } = recordingRunner();
    const previous = process.env.xbintsc_CLANG;
    process.env.xbintsc_CLANG = "my-clang";
    try {
      expect(findClang(runner)).toBe("my-clang");
      expect(calls[0]!.command).toBe("my-clang");
    } finally {
      if (previous === undefined) delete process.env.xbintsc_CLANG;
      else process.env.xbintsc_CLANG = previous;
    }
  });

  it("fails loudly when no compiler responds", () => {
    const runner: Runner = { run: () => ({ status: 127, stdout: "", stderr: "not found" }) };
    expect(() => findClang(runner)).toThrow(ToolchainError);
  });

  it("compiles IR with the requested optimization level", () => {
    const { runner, calls } = recordingRunner();
    compileIr(runner, { clang: "clang", irPath: "in.ll", objectPath: "out.o", optimize: "2" });
    expect(calls[0]!.args).toEqual([
      "-O2",
      "-ffunction-sections",
      "-fdata-sections",
      "-c",
      "in.ll",
      "-o",
      "out.o",
    ]);
  });

  it("passes -g and keeps frame pointers in debug builds", () => {
    const { runner, calls } = recordingRunner();
    compileIr(runner, { clang: "clang", irPath: "in.ll", objectPath: "out.o", optimize: "2", debug: true });
    expect(calls[0]!.args).toEqual([
      "-O2",
      "-ffunction-sections",
      "-fdata-sections",
      "-g",
      "-fno-omit-frame-pointer",
      "-c",
      "in.ll",
      "-o",
      "out.o",
    ]);
  });

  it("links objects with extra flags", () => {
    const { runner, calls } = recordingRunner();
    link(runner, { clang: "clang", objectPaths: ["a.o", "b.o"], outputPath: "prog", linkerFlags: ["-lm"], optimize: "1" });
    const deadStrip =
      process.platform === "win32" ? [] : [process.platform === "darwin" ? "-Wl,-dead_strip" : "-Wl,--gc-sections"];
    expect(calls[0]!.args).toEqual(["-O1", "a.o", "b.o", "-o", "prog", ...deadStrip, "-lm"]);
  });

  it("passes LTO and PGO flags to compilation and linking", () => {
    const { runner, calls } = recordingRunner();
    compileIr(runner, {
      clang: "clang",
      irPath: "in.ll",
      objectPath: "out.o",
      optimize: "3",
      lto: true,
      pgo: "generate",
      pgoDir: "profiles",
    });
    expect(calls[0]!.args).toEqual([
      "-O3",
      "-ffunction-sections",
      "-fdata-sections",
      "-flto",
      "-fprofile-generate=profiles",
      "-c",
      "in.ll",
      "-o",
      "out.o",
    ]);

    link(runner, {
      clang: "clang",
      objectPaths: ["a.o"],
      outputPath: "prog",
      linkerFlags: [],
      optimize: "3",
      lto: true,
      pgo: "use",
      pgoDir: "profiles",
    });
    expect(calls[1]!.args.slice(0, 4)).toEqual(["-O3", "-flto", "-fprofile-use=profiles", "-fuse-ld=lld"]);
  });

  it("merges raw profiles with llvm-profdata", () => {
    const directory = mkdtempSync(join(tmpdir(), "xbintsc-pgo-"));
    const previous = process.env.xbintsc_LLVM_PROFDATA;
    try {
      writeFileSync(join(directory, "a.profraw"), "raw");
      process.env.xbintsc_LLVM_PROFDATA = "profdata";
      const { runner, calls } = recordingRunner();
      mergeProfileData(runner, directory, "clang");
      expect(calls[0]!.command).toBe("profdata");
      expect(calls[0]!.args[0]).toBe("merge");
      expect(calls[0]!.args).toContain(join(directory, "a.profraw"));
    } finally {
      if (previous === undefined) delete process.env.xbintsc_LLVM_PROFDATA;
      else process.env.xbintsc_LLVM_PROFDATA = previous;
      rmSync(directory, { recursive: true, force: true });
    }
  });

  it("compiles C sources with an include directory", () => {
    const { runner, calls } = recordingRunner();
    compileC(runner, "clang", "runtime.c", "runtime.o", "runtime");
    expect(calls[0]!.args).toEqual([
      "-O2",
      "-Wall",
      "-Wextra",
      "-D_CRT_SECURE_NO_WARNINGS",
      "-ffunction-sections",
      "-fdata-sections",
      "-c",
      "runtime.c",
      "-o",
      "runtime.o",
      "-Iruntime",
    ]);
  });

  it("throws a ToolchainError carrying stderr on failure", () => {
    const { runner } = recordingRunner(1);
    try {
      compileIr(runner, { clang: "clang", irPath: "in.ll", objectPath: "out.o", optimize: "2" });
      expect.unreachable();
    } catch (error) {
      expect(error).toBeInstanceOf(ToolchainError);
      expect((error as ToolchainError).stderr).toBe("boom");
    }
  });
});
