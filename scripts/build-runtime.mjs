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
  // Core runtime, split into translation units (see runtime/rt_internal.h).
  ["xt_alloc.c", "xt_alloc.o"],
  ["xt_values.c", "xt_values.o"],
  ["xt_containers.c", "xt_containers.o"],
  ["xt_stdlib.c", "xt_stdlib.o"],
  ["xt_stdlib2.c", "xt_stdlib2.o"],
  ["xt_promise.c", "xt_promise.o"],
  ["xt_builtins.c", "xt_builtins.o"],
  ["xt_io.c", "xt_io.o"],
  ["xt_loop.c", "xt_loop.o"],
  // Node extension sources.
  ["ext_node/fs/read_file.c", "ext_node_fs_read_file.o"],
  ["ext_node/fs/write_file.c", "ext_node_fs_write_file.o"],
  ["ext_node/fs/fs_ops.c", "ext_node_fs_fs_ops.o"],
  ["ext_node/fs/promises.c", "ext_node_fs_promises.o"],
  ["ext_node/path/path.c", "ext_node_path_path.o"],
  ["ext_node/os/os.c", "ext_node_os_os.o"],
  ["ext_node/process/process.c", "ext_node_process_process.o"],
  ["ext_node/buffer/buffer.c", "ext_node_buffer_buffer.o"],
  ["ext_node/stream/stream.c", "ext_node_stream_stream.o"],
  ["ext_node/net/net.c", "ext_node_net_net.o"],
  ["ext_node/dgram/dgram.c", "ext_node_dgram_dgram.o"],
  ["ext_node/http/http.c", "ext_node_http_http.o"],
];

for (const [source, object] of sources) {
  const args = ["-O2", "-D_CRT_SECURE_NO_WARNINGS", "-c", join(runtimeDir, source), "-o", join(outDir, object), `-I${runtimeDir}`];
  const result = spawnSync(clang, args, { stdio: "inherit" });
  if (result.status !== 0) {
    console.error(`xbintsc: failed to compile ${source}`);
    process.exit(result.status ?? 1);
  }
}

console.log(`xbintsc: runtime objects written to ${outDir}`);
