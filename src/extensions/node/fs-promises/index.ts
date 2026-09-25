/**
 * Node's `fs/promises` module.
 *
 * There is no asynchronous I/O scheduler, so each function wraps the matching
 * synchronous `fs` implementation in an already-settled promise (rejected on
 * failure). `readFile`, `writeFile`, `mkdir`, `readdir`, `stat`, `rm`,
 * `open` (a `FileHandle`), ... are therefore importable from `fs/promises`.
 * `constants` is re-exported as a value getter.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction, ModuleExports } from "../../registry.js";

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
  cp: { symbol: "xt_node_p_cp" },
  realpath: { symbol: "xt_node_p_realpath" },
  stat: { symbol: "xt_node_p_stat" },
  lstat: { symbol: "xt_node_p_lstat" },
  statfs: { symbol: "xt_node_p_statfs" },
  access: { symbol: "xt_node_p_access" },
  open: { symbol: "xt_node_p_open" },
  chmod: { symbol: "xt_node_p_chmod" },
  lchmod: { symbol: "xt_node_p_lchmod" },
  chown: { symbol: "xt_node_p_chown" },
  lchown: { symbol: "xt_node_p_lchown" },
  truncate: { symbol: "xt_node_p_truncate" },
  utimes: { symbol: "xt_node_p_utimes" },
  lutimes: { symbol: "xt_node_p_lutimes" },
  link: { symbol: "xt_node_p_link" },
  symlink: { symbol: "xt_node_p_symlink" },
  readlink: { symbol: "xt_node_p_readlink" },
  mkdtemp: { symbol: "xt_node_p_mkdtemp" },
  opendir: { symbol: "xt_node_p_opendir" },
  glob: { symbol: "xt_node_p_glob" },
  watch: { symbol: "xt_node_p_watch" },
};

const exports: ModuleExports = {
  ...builtins,
  constants: { valueSymbol: "xt_fs_constants" },
};

export const fsPromisesModule: NodeModule = {
  name: "fs/promises",
  runtimeSources: () => [
    // Reuse the synchronous fs implementations the promise wrappers delegate to.
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/read_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/write_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fs_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fd_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/meta_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/link_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/copy_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/constants.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/promises.c"),
  ],
  builtins: () => builtins,
  exports: () => exports,
};
