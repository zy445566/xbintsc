#!/usr/bin/env node
/**
 * Prerequisite: compile the C runtime (and the node extension) into
 * object files under build/runtime. The driver normally compiles these lazily
 * into its cache; this script is for development and CI smoke checks.
 */

import { mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const runtimeDir = join(root, "runtime");
const outDir = join(root, "build", "runtime");
mkdirSync(outDir, { recursive: true });

const clang = process.env.xbintsc_CLANG ?? "clang";

const sources = [
  ["xt_runtime.c", "xt_runtime.o"],
  ["ext_node/fs/read_file.c", "ext_node_fs_read_file.o"],
];

for (const [source, object] of sources) {
  const args = ["-O2", "-c", join(runtimeDir, source), "-o", join(outDir, object), `-I${runtimeDir}`];
  const result = spawnSync(clang, args, { stdio: "inherit" });
  if (result.status !== 0) {
    console.error(`xbintsc: failed to compile ${source}`);
    process.exit(result.status ?? 1);
  }
}

console.log(`xbintsc: runtime objects written to ${outDir}`);
