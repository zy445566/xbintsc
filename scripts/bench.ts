#!/usr/bin/env node
/**
 * Build and time a small, fixed benchmark suite.
 *
 * The numbers are wall-clock times on the current machine, so they are only
 * useful for comparing revisions of the compiler (or the same revision against
 * Node): run `npm run bench` before and after a change to the runtime or
 * codegen. Each program is compiled with the normal driver (cache bypassed) and
 * executed several times; the table reports the fastest run to reduce noise.
 *
 * Usage:
 *   npm run bench                 # every benchmark, 3 runs, with Node for scale
 *   npm run bench -- --runs 5     # more runs (minimum is reported)
 *   npm run bench -- --filter prop
 *   npm run bench -- --no-node    # skip the Node comparison
 */

import { spawnSync } from "node:child_process";
import { mkdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "../src/driver/index.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = join(root, "build", "bench");
const cacheDir = join(root, "build", "bench-cache");

interface Benchmark {
  readonly name: string;
  /** Plain JavaScript so the same source can run under Node. */
  readonly source: string;
}

const BENCHMARKS: readonly Benchmark[] = [
  {
    name: "numeric",
    source: `
let total = 0;
for (let i = 0; i < 50000000; i++) total = total + i;
console.log(total);
`,
  },
  {
    name: "property",
    source: `
const object = { x: 0 };
for (let i = 0; i < 5000000; i++) object.x = object.x + 1;
console.log(object.x);
`,
  },
  {
    name: "array",
    source: `
const values = [];
for (let i = 0; i < 3000000; i++) values[i] = i;
let total = 0;
for (let i = 0; i < 3000000; i++) total = total + values[i];
console.log(total);
`,
  },
  {
    name: "string-concat",
    source: `
let text = "";
for (let i = 0; i < 100000; i++) text = text + "a";
console.log(text.length);
`,
  },
];

interface Options {
  readonly runs: number;
  readonly filter?: string;
  readonly node: boolean;
}

function parseOptions(argv: readonly string[]): Options {
  let runs = 3;
  let filter: string | undefined;
  let node = true;
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index]!;
    if (arg === "--no-node") node = false;
    else if (arg === "--runs") runs = Math.max(1, Number(argv[++index]) || 1);
    else if (arg.startsWith("--runs=")) runs = Math.max(1, Number(arg.slice("--runs=".length)) || 1);
    else if (arg === "--filter") filter = argv[++index];
    else if (arg.startsWith("--filter=")) filter = arg.slice("--filter=".length);
    else if (arg === "--help" || arg === "-h") {
      process.stdout.write(
        "usage: npm run bench [-- --runs N] [--filter <substr>] [--no-node]\n",
      );
      process.exit(0);
    }
  }
  return { runs, filter, node };
}

function minTimeMs(runs: number, execute: () => void): number {
  let best = Number.POSITIVE_INFINITY;
  for (let run = 0; run < runs; run++) {
    const start = process.hrtime.bigint();
    execute();
    const elapsed = Number(process.hrtime.bigint() - start) / 1e6;
    if (elapsed < best) best = elapsed;
  }
  return best;
}

function formatMs(value: number | undefined): string {
  return value === undefined ? "-" : `${value.toFixed(1)} ms`;
}

function main(): number {
  const options = parseOptions(process.argv.slice(2));
  mkdirSync(outDir, { recursive: true });
  mkdirSync(cacheDir, { recursive: true });
  const benchmarks = BENCHMARKS.filter(
    (benchmark) => options.filter === undefined || benchmark.name.includes(options.filter),
  );
  if (benchmarks.length === 0) {
    process.stderr.write(`xbintsc: no benchmark matches '${options.filter}'\n`);
    return 1;
  }

  const rows: { name: string; ours?: number; node?: number; bytes?: number }[] = [];
  for (const benchmark of benchmarks) {
    const entry = join(outDir, `${benchmark.name}.ts`);
    writeFileSync(entry, benchmark.source);
    const result = build(entry, {
      emit: "exe",
      outDir: join(outDir, "bin"),
      cacheDir,
      force: true,
    });
    const errors = result.diagnostics.filter((diagnostic) => diagnostic.category === "error");
    if (errors.length > 0) {
      for (const error of errors) process.stderr.write(`xbintsc: ${error.message}\n`);
      return 1;
    }
    const bytes = statSync(result.outputPath).size;
    const ours = minTimeMs(options.runs, () => {
      const executed = spawnSync(result.outputPath, [], { stdio: "ignore" });
      if (executed.status !== 0) throw new Error(`${benchmark.name} exited with ${executed.status}`);
    });

    let nodeTime: number | undefined;
    if (options.node) {
      const script = join(outDir, `${benchmark.name}.mjs`);
      writeFileSync(script, benchmark.source);
      nodeTime = minTimeMs(options.runs, () => {
        const executed = spawnSync(process.execPath, [script], { stdio: "ignore" });
        if (executed.status !== 0) throw new Error(`${benchmark.name} (node) exited with ${executed.status}`);
      });
    }
    rows.push({ name: benchmark.name, ours, node: nodeTime, bytes });
  }

  const header = ["benchmark", "xbintsc", "node", "size"];
  process.stdout.write(
    `${header[0]!.padEnd(16)}${header[1]!.padStart(12)}${header[2]!.padStart(12)}${header[3]!.padStart(12)}\n`,
  );
  for (const row of rows) {
    const size = row.bytes === undefined ? "-" : `${Math.round(row.bytes / 1024)} KB`;
    process.stdout.write(
      `${row.name.padEnd(16)}${formatMs(row.ours).padStart(12)}${formatMs(row.node).padStart(12)}${size.padStart(12)}\n`,
    );
  }
  process.stdout.write(`\nfastest of ${options.runs} run(s) on ${process.platform}-${process.arch}\n`);
  return 0;
}

process.exit(main());
