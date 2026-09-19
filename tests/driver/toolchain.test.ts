import { describe, expect, it } from "vitest";
import {
  compileC,
  compileIr,
  findClang,
  link,
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
    expect(calls[0]!.args).toEqual(["-O2", "-c", "in.ll", "-o", "out.o"]);
  });

  it("links objects with extra flags", () => {
    const { runner, calls } = recordingRunner();
    link(runner, { clang: "clang", objectPaths: ["a.o", "b.o"], outputPath: "prog", linkerFlags: ["-lm"], optimize: "1" });
    expect(calls[0]!.args).toEqual(["-O1", "a.o", "b.o", "-o", "prog", "-lm"]);
  });

  it("compiles C sources with an include directory", () => {
    const { runner, calls } = recordingRunner();
    compileC(runner, "clang", "runtime.c", "runtime.o", "runtime");
    expect(calls[0]!.args).toEqual(["-O2", "-D_CRT_SECURE_NO_WARNINGS", "-c", "runtime.c", "-o", "runtime.o", "-Iruntime"]);
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
