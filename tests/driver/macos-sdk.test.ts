import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { macosSdkRoot } from "../../src/driver/macos-sdk.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";
import { sysrootFlags } from "../../src/driver/toolchain.js";

const directories: string[] = [];

/** A real directory, standing in for an installed SDK. */
function temporarySdk(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-sdk-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

function runnerReturning(answer: CommandResult | (() => CommandResult)): Runner {
  return {
    run(): CommandResult {
      return typeof answer === "function" ? answer() : answer;
    },
  };
}

describe("macosSdkRoot", () => {
  it("does nothing off macOS", () => {
    const commands: string[] = [];
    const runner: Runner = {
      run(command: string): CommandResult {
        commands.push(command);
        return { status: 0, stdout: "/sdk", stderr: "" };
      },
    };
    expect(macosSdkRoot(runner, {}, "linux")).toBeUndefined();
    expect(macosSdkRoot(runner, {}, "win32")).toBeUndefined();
    expect(commands).toEqual([]);
  });

  it("keeps an explicit SDKROOT", () => {
    const runner = runnerReturning({ status: 0, stdout: temporarySdk(), stderr: "" });
    expect(macosSdkRoot(runner, { SDKROOT: "/chosen/by/the/user" }, "darwin")).toBeUndefined();
  });

  it("adopts the SDK xcrun reports", () => {
    const sdk = temporarySdk();
    const calls: { command: string; args: readonly string[] }[] = [];
    const runner: Runner = {
      run(command: string, args: readonly string[]): CommandResult {
        calls.push({ command, args });
        return { status: 0, stdout: `${sdk}\n`, stderr: "" };
      },
    };
    expect(macosSdkRoot(runner, {}, "darwin")).toBe(sdk);
    expect(calls).toEqual([{ command: "xcrun", args: ["--show-sdk-path"] }]);
  });

  it("ignores an xcrun that fails", () => {
    const runner = runnerReturning({ status: 72, stdout: "", stderr: "xcrun: error: unable to find SDK" });
    expect(macosSdkRoot(runner, {}, "darwin")).toBeUndefined();
  });

  it("ignores an empty or stale SDK path", () => {
    expect(macosSdkRoot(runnerReturning({ status: 0, stdout: "  \n", stderr: "" }), {}, "darwin")).toBeUndefined();
    const stale = join(tmpdir(), "xbintsc-sdk-does-not-exist");
    expect(macosSdkRoot(runnerReturning({ status: 0, stdout: stale, stderr: "" }), {}, "darwin")).toBeUndefined();
  });

  it("survives a runner that throws", () => {
    const runner: Runner = {
      run(): CommandResult {
        throw new Error("cannot spawn xcrun");
      },
    };
    expect(macosSdkRoot(runner, {}, "darwin")).toBeUndefined();
  });
});

describe("sysrootFlags", () => {
  it("passes the SDK on the command line", () => {
    expect(sysrootFlags("/Applications/Xcode.app/SDKs/MacOSX.sdk")).toEqual([
      "-isysroot",
      "/Applications/Xcode.app/SDKs/MacOSX.sdk",
    ]);
  });

  it("adds nothing when no SDK was resolved", () => {
    expect(sysrootFlags(undefined)).toEqual([]);
  });
});
