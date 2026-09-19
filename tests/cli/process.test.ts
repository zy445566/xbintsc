/**
 * Runs the CLI as a real child process.
 *
 * This guards a class of bug that only shows up off the developer's machine:
 * whether `bin`/`main` actually dispatches `run()` when the file is executed as
 * the program entry point (the previous string-built `file://` comparison never
 * matched on Windows, so `xtsc build` silently did nothing).
 */

import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";

const projectRoot = resolve(fileURLToPath(new URL("../..", import.meta.url)));
const cliEntry = join(projectRoot, "src", "cli", "main.ts");

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xtsc-process-"));
  directories.push(directory);
  return directory;
}

function runCli(args: readonly string[]): { status: number | null; stdout: string; stderr: string } {
  const result = spawnSync(process.execPath, ["--import", "tsx", cliEntry, ...args], {
    cwd: projectRoot,
    encoding: "utf8",
  });
  return { status: result.status, stdout: result.stdout ?? "", stderr: result.stderr ?? "" };
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("cli process entry point", () => {
  it("actually runs when invoked as the entry point", () => {
    const { status, stdout } = runCli(["version"]);
    expect(status).toBe(0);
    expect(stdout).toMatch(/xtsc \d+\.\d+\.\d+/);
  });

  it("emits IR to stdout", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1 + 1);");
    const { status, stdout } = runCli(["emit", entry]);
    expect(status).toBe(0);
    expect(stdout).toContain("define i32 @main");
  });

  it("exits non-zero with diagnostics for invalid input", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "bad.ts");
    writeFileSync(entry, "const = ;");
    const { status, stderr } = runCli(["emit", entry]);
    expect(status).toBe(1);
    expect(stderr).toContain("error TS");
  });
});
