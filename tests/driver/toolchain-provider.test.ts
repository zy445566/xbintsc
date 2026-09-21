import { afterEach, describe, expect, it } from "vitest";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";

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

  it("falls back to the system PATH", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    const toolchain = resolveToolchain(runnerThatWorks("clang"));
    expect(toolchain.clang).toBe("clang");
    expect(toolchain.source).toBe("system");
  });

  it("reads extra linker args from xbintsc_LINKER_ARGS", () => {
    process.env.xbintsc_CLANG = "my-clang";
    process.env.xbintsc_LINKER_ARGS = "-fuse-ld=lld --foo";
    const toolchain = resolveToolchain(runnerThatWorks("my-clang"));
    expect(toolchain.linkerArgs).toEqual(["-fuse-ld=lld", "--foo"]);
  });
});
