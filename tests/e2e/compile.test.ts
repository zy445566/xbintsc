/**
 * End-to-end tests: compile TypeScript all the way to a native binary and run
 * it. Skipped automatically when no clang-compatible compiler is available.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

describeWithClang("end-to-end compilation", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-e2e-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function runProgram(source: string, options: { extensions?: boolean; name?: string } = {}): string {
    const name = options.name ?? `program_${Math.random().toString(36).slice(2)}`;
    const entry = join(workdir, `${name}.ts`);
    writeFileSync(entry, source);
    const extensions = options.extensions ? createDefaultRegistry().register(nodeExtension) : undefined;
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir,
      extensions,
    });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], { encoding: "utf8" });
    expect(executed.status).toBe(0);
    return executed.stdout.trim();
  }

  it("evaluates arithmetic and prints results", () => {
    expect(runProgram('console.log("sum", 2 + 3 * 4);')).toBe("sum 14");
  });

  it("runs recursive functions", () => {
    const source = `
      function fib(n: number): number {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
      }
      console.log(fib(10));
    `;
    expect(runProgram(source)).toBe("55");
  });

  it("supports loops, arrays and string concatenation", () => {
    const source = `
      const numbers = [1, 2, 3, 4];
      let total = 0;
      for (const n of numbers) {
        total += n;
      }
      console.log("total = " + total);
    `;
    expect(runProgram(source)).toBe("total = 10");
  });

  it("implements closures with by-reference captures", () => {
    const source = `
      const makeCounter = () => {
        let count = 0;
        return () => {
          count += 1;
          return count;
        };
      };
      const next = makeCounter();
      console.log(next(), next(), next());
    `;
    expect(runProgram(source)).toBe("1 2 3");
  });

  it("prints objects and arrays with JS-like formatting", () => {
    const source = `
      const user = { name: "ada", tags: ["math", "code"], age: 36 };
      console.log(user);
    `;
    expect(runProgram(source)).toBe("{ name: ada, tags: [ math, code ], age: 36 }");
  });

  it("reads files through the node extension", () => {
    const dataPath = join(workdir, "data.txt");
    writeFileSync(dataPath, "from a file");
    const source = `console.log(readFileSync(${JSON.stringify(dataPath)}));`;
    expect(runProgram(source, { extensions: true })).toBe("from a file");
  });
});
