import { afterEach, describe, expect, it } from "vitest";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";
import { ToolchainError } from "../../src/driver/toolchain.js";

const saved = { ...process.env };

afterEach(() => {
  process.env = { ...saved };
});

/** A runner that reports success only for the named commands. */
function runnerThatWorks(...commands: string[]): Runner {
  return {
    run(command: string): CommandResult {
      return { status: commands.includes(command) ? 0 : 1, stdout: "", stderr: "" };
    },
  };
}

describe("resolveToolchain", () => {
  it("prefers an explicit xbintsc_CLANG", () => {
    process.env.xbintsc_CLANG = "my-clang";
    const toolchain = resolveToolchain(runnerThatWorks("my-clang"));
    expect(toolchain.clang).toBe("my-clang");
    expect(toolchain.source).toBe("env");
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
        return { status: command === "clang" ? 0 : 1, stdout: "", stderr: "" };
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
