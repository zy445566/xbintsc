import { afterEach, describe, expect, it } from "vitest";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";
import { isMsvcTarget, parseClangTarget, ToolchainError } from "../../src/driver/toolchain.js";

const saved = { ...process.env };

afterEach(() => {
  process.env = { ...saved };
});

const CLANG_19 = "clang version 19.1.5 (https://github.com/llvm/llvm-project.git)";
const CLANG_15 = "clang version 15.0.2";
const CLANG_18 = "Ubuntu clang version 18.1.3 (1ubuntu1)";
const CLANG_19_MSVC = "clang version 19.1.5\nTarget: x86_64-pc-windows-msvc\nThread model: posix";
const CLANG_20_GNU = "clang version 20.1.0\nTarget: x86_64-w64-windows-gnu\nThread model: posix";

describe("clang target parsing", () => {
  it("reads the Target line", () => {
    expect(parseClangTarget(CLANG_19_MSVC)).toBe("x86_64-pc-windows-msvc");
    expect(parseClangTarget(CLANG_19)).toBeUndefined();
  });

  it("recognises only MSVC Windows targets", () => {
    expect(isMsvcTarget("x86_64-pc-windows-msvc")).toBe(true);
    expect(isMsvcTarget("aarch64-pc-windows-msvc")).toBe(true);
    expect(isMsvcTarget("x86_64-w64-windows-gnu")).toBe(false);
    expect(isMsvcTarget("x86_64-pc-linux-gnu")).toBe(false);
  });
});

/** A runner that reports success only for the named commands. */
function runnerThatWorks(...commands: string[]): Runner {
  return {
    run(command: string): CommandResult {
      const works = commands.includes(command);
      return { status: works ? 0 : 1, stdout: works ? CLANG_19 : "", stderr: "" };
    },
  };
}

/** A runner whose `--version` output depends on the command. */
function runnerReturning(outputs: Record<string, string>): Runner {
  return {
    run(command: string): CommandResult {
      const output = outputs[command];
      if (output === undefined) return { status: 1, stdout: "", stderr: "" };
      return { status: 0, stdout: output, stderr: "" };
    },
  };
}

describe("resolveToolchain", () => {
  it("prefers an explicit xbintsc_CLANG", () => {
    process.env.xbintsc_CLANG = "my-clang";
    const toolchain = resolveToolchain(runnerThatWorks("my-clang"));
    expect(toolchain.clang).toBe("my-clang");
    expect(toolchain.source).toBe("env");
    expect(toolchain.version).toEqual({ major: 19, minor: 1, patch: 5 });
  });

  it("falls back to xbintsc_TOOLCHAIN when xbintsc_CLANG is unset", () => {
    delete process.env.xbintsc_CLANG;
    process.env.xbintsc_TOOLCHAIN = "toolchain-clang";
    const toolchain = resolveToolchain(runnerThatWorks("toolchain-clang"));
    expect(toolchain.clang).toBe("toolchain-clang");
    expect(toolchain.source).toBe("env");
  });

  it("ignores an explicit compiler that fails --version", () => {
    process.env.xbintsc_CLANG = "broken-clang";
    const toolchain = resolveToolchain(runnerThatWorks("clang"));
    expect(toolchain.clang).toBe("clang");
    expect(toolchain.source).toBe("system");
  });

  it("falls back to the system PATH", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const toolchain = resolveToolchain(runnerThatWorks("clang"));
    expect(toolchain.clang).toBe("clang");
    expect(toolchain.source).toBe("system");
    expect(toolchain.env).toEqual({});
    expect(toolchain.linkerArgs).toEqual([]);
  });

  it("rejects a system compiler older than the minimum", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const runner = runnerReturning({ clang: CLANG_15 });
    expect(() => resolveToolchain(runner)).toThrow(ToolchainError);
    expect(() => resolveToolchain(runner)).toThrow(/requires clang 16/);
  });

  it("prefers a newer versioned binary when `clang` itself is too old", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const toolchain = resolveToolchain(runnerReturning({ clang: CLANG_15, "clang-18": CLANG_18 }));
    expect(toolchain.clang).toBe("clang-18");
    expect(toolchain.version?.major).toBe(18);
  });

  it.runIf(process.platform === "win32")("skips a non-MSVC clang and keeps looking", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const toolchain = resolveToolchain(
      runnerReturning({ clang: CLANG_20_GNU, "clang-16": CLANG_19_MSVC }),
    );
    expect(toolchain.clang).toBe("clang-16");
    expect(toolchain.version?.major).toBe(19);
  });

  it.runIf(process.platform === "win32")("rejects an explicit non-MSVC clang", () => {
    process.env.xbintsc_CLANG = "gnu-clang";
    const runner = runnerReturning({ "gnu-clang": CLANG_20_GNU, clang: CLANG_19_MSVC });
    expect(() => resolveToolchain(runner)).toThrow(/targets x86_64-w64-windows-gnu/);
  });

  it("rejects an explicit compiler that is too old instead of silently switching", () => {
    process.env.xbintsc_CLANG = "old-clang";
    const runner = runnerReturning({ "old-clang": CLANG_15, clang: CLANG_19 });
    expect(() => resolveToolchain(runner)).toThrow(/old-clang reports clang 15\.0\.2/);
  });

  it("accepts an explicit wrapper that prints no clang-style version", () => {
    process.env.xbintsc_CLANG = "wrapped-driver";
    const toolchain = resolveToolchain(runnerReturning({ "wrapped-driver": "my custom driver 1.0" }));
    expect(toolchain.clang).toBe("wrapped-driver");
    expect(toolchain.version).toBeUndefined();
  });

  it("skips a `cc` that is not clang", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const toolchain = resolveToolchain(
      runnerReturning({ cc: "cc (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0", "clang-16": "clang version 16.0.6" }),
    );
    expect(toolchain.clang).toBe("clang-16");
  });

  it("reads extra linker args from xbintsc_LINKER_ARGS", () => {
    process.env.xbintsc_CLANG = "my-clang";
    process.env.xbintsc_LINKER_ARGS = "-fuse-ld=lld --foo";
    const toolchain = resolveToolchain(runnerThatWorks("my-clang"));
    expect(toolchain.linkerArgs).toEqual(["-fuse-ld=lld", "--foo"]);
  });

  it("ignores empty entries in xbintsc_LINKER_ARGS", () => {
    process.env.xbintsc_CLANG = "my-clang";
    process.env.xbintsc_LINKER_ARGS = "  --foo   --bar ";
    const toolchain = resolveToolchain(runnerThatWorks("my-clang"));
    expect(toolchain.linkerArgs).toEqual(["--foo", "--bar"]);
  });

  it("still succeeds when an explicit compiler throws", () => {
    process.env.xbintsc_CLANG = "exploding-clang";
    const runner: Runner = {
      run(command: string): CommandResult {
        if (command === "exploding-clang") throw new Error("cannot spawn");
        return { status: command === "clang" ? 0 : 1, stdout: command === "clang" ? CLANG_19 : "", stderr: "" };
      },
    };
    const toolchain = resolveToolchain(runner);
    expect(toolchain.source).toBe("system");
    expect(toolchain.clang).toBe("clang");
  });

  it("throws when no compiler can be found at all", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const runner: Runner = {
      run(): CommandResult {
        return { status: 1, stdout: "", stderr: "missing" };
      },
    };
    expect(() => resolveToolchain(runner)).toThrow(ToolchainError);
  });
});
