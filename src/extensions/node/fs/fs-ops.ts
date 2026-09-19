/**
 * Builtins for the filesystem operations in Node's `fs` module
 * (`existsSync`, directory handling, rename/copy, `statSync`, ...).
 */

import type { BuiltinFunction } from "../../registry.js";

export const fsOpsBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  existsSync: { symbol: "xt_node_exists" },
  readdirSync: { symbol: "xt_node_read_dir" },
  mkdirSync: { symbol: "xt_node_mkdir" },
  rmSync: { symbol: "xt_node_rm" },
  unlinkSync: { symbol: "xt_node_unlink" },
  rmdirSync: { symbol: "xt_node_rmdir" },
  renameSync: { symbol: "xt_node_rename" },
  copyFileSync: { symbol: "xt_node_copy_file" },
  realpathSync: { symbol: "xt_node_realpath" },
  statSync: { symbol: "xt_node_stat" },
  lstatSync: { symbol: "xt_node_lstat" },
};
