import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";
import { ToolchainError } from "../../src/driver/toolchain.js";

// The real `findVendorDir` also looks next to the package, so a toolchain
// already fetched on the machine (e.g. the CI `vendor/` for Windows ARM64)
// would shadow the temporary one each test creates. Restrict discovery to the
// working directory so the tests are hermetic.
vi.mock("../../src/driver/paths.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/driver/paths.js")>();
  const { existsSync } = await import("node:fs");
  const { join: joinPath } = await import("node:path");
  return {
    ...actual,
    findVendorDir: (): string | undefined => {
      const root = process.cwd();
      const specific = joinPath(root, "vendor", `${process.platform}-${process.arch}`);
      if (existsSync(specific)) return specific;
      const generic = joinPath(root, "vendor");
      return existsSync(generic) ? generic : undefined;
    },
  };
});

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

/** Run `fn` inside a temporary working directory and always restore it. */
function withWorkingDirectory<T>(fn: (directory: string) => T): T {
  const created = mkdtempSync(join(tmpdir(), "xbintsc-toolchain-"));
  const previous = process.cwd();
  process.chdir(created);
  // `process.cwd()` may resolve symlinks (e.g. /var -> /private/var on macOS),
  // so hand the caller the canonical path the provider will actually see.
  const directory = process.cwd();
  try {
    return fn(directory);
  } finally {
    process.chdir(previous);
    rmSync(created, { recursive: true, force: true });
  }
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

  it("discovers a bundled vendor toolchain and prefers lld", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    withWorkingDirectory((directory) => {
      mkdirSync(join(directory, "vendor", "bin"), { recursive: true });
      const clang = join(directory, "vendor", "bin", "clang");
      writeFileSync(clang, "");
      const toolchain = resolveToolchain(runnerThatWorks(clang));
      expect(toolchain.source).toBe("vendor");
      expect(toolchain.clang).toBe(clang);
      expect(toolchain.linkerArgs).toContain("-fuse-ld=lld");
    });
  });

  it("ignores a vendor directory whose compiler does not work", () => {
    delete process.env.xbintsc_CLANG;
    delete process.env.xbintsc_TOOLCHAIN;
    withWorkingDirectory((directory) => {
      mkdirSync(join(directory, "vendor", "bin"), { recursive: true });
      writeFileSync(join(directory, "vendor", "bin", "clang"), "");
      const toolchain = resolveToolchain(runnerThatWorks("clang"));
      expect(toolchain.source).toBe("system");
      expect(toolchain.clang).toBe("clang");
    });
  });

  it("still succeeds when an explicit compiler throws and a vendor exists", () => {
    process.env.xbintsc_CLANG = "exploding-clang";
    withWorkingDirectory((directory) => {
      mkdirSync(join(directory, "vendor", "bin"), { recursive: true });
      const clang = join(directory, "vendor", "bin", "clang");
      writeFileSync(clang, "");
      const runner: Runner = {
        run(command: string): CommandResult {
          if (command === "exploding-clang") throw new Error("cannot spawn");
          return { status: command === clang ? 0 : 1, stdout: "", stderr: "" };
        },
      };
      const toolchain = resolveToolchain(runner);
      expect(toolchain.source).toBe("vendor");
    });
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
