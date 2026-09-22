/**
 * Shared harness for the end-to-end tests that compile TypeScript all the way
 * to a native binary and run it. Suites are skipped automatically when no
 * clang-compatible compiler is available.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

export interface RunOptions {
  /** Link the Node compatibility extension. */
  extensions?: boolean;
  /** Entry file base name; defaults to a random name. */
  name?: string;
  /** Force a fresh build, bypassing the whole-program cache. */
  force?: boolean;
  /** Child-process timeout in milliseconds. */
  timeout?: number;
  /** Extra modules written next to the entry file, keyed by relative path. */
  files?: Record<string, string>;
}

export interface RunResult {
  stdout: string;
  stderr: string;
  status: number | null;
}

export interface NodeRunOptions {
  /** Entry file base name; defaults to a random name. */
  name?: string;
  /** Child-process timeout in milliseconds. */
  timeout?: number;
  /** Extra modules written next to the entry file, keyed by relative path. */
  files?: Record<string, string>;
}

export interface E2EHarness {
  /** Scratch directory shared by the tests in the calling suite. */
  readonly workdir: string;
  /** Compile and run `source`, asserting a clean build and zero exit status. */
  runProgram(source: string, options?: RunOptions): string;
  /** Compile and run `source` without asserting the exit status. */
  runProgramFull(source: string, options?: RunOptions): RunResult;
  /**
   * Run `source` under Node (through the `tsx` loader) and return its trimmed
   * stdout. Differential cases should print canonical `JSON.stringify` output
   * so console formatting never masks a real difference.
   */
  runNodeProgram(source: string, options?: NodeRunOptions): string;
  /**
   * Compile `source` with xbintsc and run it under Node, asserting both produce
   * byte-identical stdout. This is the core differential-testing primitive.
   */
  expectSameOutputAsNode(source: string, options?: RunOptions): void;
}

/**
 * Declare a suite of end-to-end compilation tests. The `define` callback runs
 * inside the `describe` scope and receives the shared harness, so suites can
 * register `it(...)` cases directly.
 */
export function describeE2E(
  name: string,
  define: (harness: E2EHarness) => void,
  options: { tmpPrefix?: string } = {},
): void {
  describeWithClang(name, () => {
    let workdir: string;
    let cacheDir: string;

    beforeAll(() => {
      workdir = mkdtempSync(join(tmpdir(), options.tmpPrefix ?? "xbintsc-e2e-"));
      cacheDir = join(workdir, ".cache");
    });

    afterAll(() => {
      rmSync(workdir, { recursive: true, force: true });
    });

    const runProgramFull = (source: string, runOptions: RunOptions = {}): RunResult => {
      const entry = join(workdir, `${runOptions.name ?? `program_${Math.random().toString(36).slice(2)}`}.ts`);
      for (const [relative, contents] of Object.entries(runOptions.files ?? {})) {
        writeFileSync(join(workdir, relative), contents);
      }
      writeFileSync(entry, source);
      const extensions = runOptions.extensions ? createDefaultRegistry().register(nodeExtension) : undefined;
      const result = build(entry, {
        emit: "exe",
        outDir: join(workdir, "out"),
        cacheDir,
        extensions,
        ...(runOptions.force ? { force: true } : {}),
      });
      expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
      const executed = spawnSync(result.outputPath, [], {
        encoding: "utf8",
        ...(runOptions.timeout ? { timeout: runOptions.timeout } : {}),
      });
      return { stdout: executed.stdout ?? "", stderr: executed.stderr ?? "", status: executed.status };
    };

    const runNodeProgram = (source: string, runOptions: NodeRunOptions = {}): string => {
      const entry = join(workdir, `${runOptions.name ?? `node_${Math.random().toString(36).slice(2)}`}.ts`);
      for (const [relative, contents] of Object.entries(runOptions.files ?? {})) {
        writeFileSync(join(workdir, relative), contents);
      }
      writeFileSync(entry, source);
      const executed = spawnSync(process.execPath, ["--import", "tsx", entry], {
        encoding: "utf8",
        ...(runOptions.timeout ? { timeout: runOptions.timeout } : {}),
      });
      expect(executed.status).toBe(0);
      return (executed.stdout ?? "").trim();
    };

    const harness: E2EHarness = {
      get workdir() {
        return workdir;
      },
      runProgram(source: string, runOptions: RunOptions = {}): string {
        const result = runProgramFull(source, runOptions);
        expect(result.status).toBe(0);
        return result.stdout.trim();
      },
      runProgramFull,
      runNodeProgram,
      expectSameOutputAsNode(source: string, runOptions: RunOptions = {}): void {
        const ours = harness.runProgram(source, runOptions);
        const theirs = runNodeProgram(source, {
          ...(runOptions.name ? { name: runOptions.name } : {}),
          ...(runOptions.timeout ? { timeout: runOptions.timeout } : {}),
          ...(runOptions.files ? { files: runOptions.files } : {}),
        });
        expect(ours).toBe(theirs);
      },
    };

    define(harness);
  });
}
