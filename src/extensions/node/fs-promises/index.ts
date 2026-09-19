/**
 * Node's `fs/promises` module.
 *
 * There is no asynchronous I/O scheduler, so each function wraps the matching
 * synchronous `fs` implementation in an already-resolved promise. `readFile`,
 * `writeFile`, `mkdir`, `readdir`, `stat`, `rm`, ... are therefore importable
 * from `fs/promises`, returning promises.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction } from "../../registry.js";

const builtins: Readonly<Record<string, BuiltinFunction>> = {
  readFile: { symbol: "xt_node_p_read_file" },
  writeFile: { symbol: "xt_node_p_write_file" },
  appendFile: { symbol: "xt_node_p_append_file" },
  mkdir: { symbol: "xt_node_p_mkdir" },
  readdir: { symbol: "xt_node_p_readdir" },
  rm: { symbol: "xt_node_p_rm" },
  unlink: { symbol: "xt_node_p_unlink" },
  rmdir: { symbol: "xt_node_p_rmdir" },
  rename: { symbol: "xt_node_p_rename" },
  copyFile: { symbol: "xt_node_p_copy_file" },
  realpath: { symbol: "xt_node_p_realpath" },
  stat: { symbol: "xt_node_p_stat" },
  lstat: { symbol: "xt_node_p_lstat" },
  access: { symbol: "xt_node_p_access" },
};

export const fsPromisesModule: NodeModule = {
  name: "fs/promises",
  runtimeSources: () => [
    // Reuse the synchronous fs implementations the promise wrappers delegate to.
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/read_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/write_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fs_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/promises.c"),
  ],
  builtins: () => builtins,
};
