/**
 * Builtins for the filesystem operations in Node's `fs` module.
 *
 * Every runtime symbol uses the uniform `xt_value fn(int32_t, xt_value *)`
 * ABI. xbintsc has no event loop, so only the synchronous API is exposed
 * (`*Sync`), plus the watcher factories, whose objects never fire events.
 */

import type { BuiltinFunction } from "../../registry.js";

export const fsOpsBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  // Path / metadata -------------------------------------------------------
  existsSync: { symbol: "xt_node_exists" },
  accessSync: { symbol: "xt_node_access" },
  statSync: { symbol: "xt_node_stat" },
  lstatSync: { symbol: "xt_node_lstat" },
  statfsSync: { symbol: "xt_node_statfs" },
  realpathSync: { symbol: "xt_node_realpath" },
  chmodSync: { symbol: "xt_node_chmod" },
  lchmodSync: { symbol: "xt_node_lchmod" },
  chownSync: { symbol: "xt_node_chown" },
  lchownSync: { symbol: "xt_node_lchown" },
  truncateSync: { symbol: "xt_node_truncate" },
  utimesSync: { symbol: "xt_node_utimes" },
  lutimesSync: { symbol: "xt_node_lutimes" },

  // Directories / entries -------------------------------------------------
  readdirSync: { symbol: "xt_node_read_dir" },
  mkdirSync: { symbol: "xt_node_mkdir" },
  mkdtempSync: { symbol: "xt_node_mkdtemp" },
  rmdirSync: { symbol: "xt_node_rmdir" },
  rmSync: { symbol: "xt_node_rm" },
  unlinkSync: { symbol: "xt_node_unlink" },
  renameSync: { symbol: "xt_node_rename" },
  copyFileSync: { symbol: "xt_node_copy_file" },
  cpSync: { symbol: "xt_node_cp" },
  linkSync: { symbol: "xt_node_link" },
  symlinkSync: { symbol: "xt_node_symlink" },
  readlinkSync: { symbol: "xt_node_readlink" },
  opendirSync: { symbol: "xt_node_opendir" },
  globSync: { symbol: "xt_node_glob" },

  // File descriptors ------------------------------------------------------
  openSync: { symbol: "xt_node_open" },
  closeSync: { symbol: "xt_node_close" },
  readSync: { symbol: "xt_node_read" },
  writeSync: { symbol: "xt_node_write" },
  readvSync: { symbol: "xt_node_readv" },
  writevSync: { symbol: "xt_node_writev" },
  fstatSync: { symbol: "xt_node_fstat" },
  fsyncSync: { symbol: "xt_node_fsync" },
  fdatasyncSync: { symbol: "xt_node_fdatasync" },
  ftruncateSync: { symbol: "xt_node_ftruncate" },
  fchmodSync: { symbol: "xt_node_fchmod" },
  fchownSync: { symbol: "xt_node_fchown" },
  futimesSync: { symbol: "xt_node_futimes" },

  // Watchers (API-shaped, no events) --------------------------------------
  watch: { symbol: "xt_node_watch" },
  watchFile: { symbol: "xt_node_watch_file" },
  unwatchFile: { symbol: "xt_node_unwatch_file" },
};
