/**
 * The callback forms of Node's `fs` API.
 *
 * Each name forwards to the promise implementation in `callbacks.c`, which
 * attaches `callback(null, value)` / `callback(error)` handlers. `exists` keeps
 * Node's odd shape: one boolean argument and no error.
 */

import type { BuiltinFunction } from "../../registry.js";

const callback = (name: string): BuiltinFunction => ({ symbol: `xt_node_${name}_cb` });

export const fsCallbackBuiltins: Readonly<Record<string, BuiltinFunction>> = {
  readFile: callback("read_file"),
  writeFile: callback("write_file"),
  appendFile: callback("append_file"),
  mkdir: callback("mkdir"),
  mkdtemp: callback("mkdtemp"),
  readdir: callback("readdir"),
  rm: callback("rm"),
  rmdir: callback("rmdir"),
  unlink: callback("unlink"),
  rename: callback("rename"),
  copyFile: callback("copy_file"),
  cp: callback("cp"),
  realpath: callback("realpath"),
  stat: callback("stat"),
  lstat: callback("lstat"),
  statfs: callback("statfs"),
  access: callback("access"),
  open: callback("open"),
  chmod: callback("chmod"),
  lchmod: callback("lchmod"),
  chown: callback("chown"),
  lchown: callback("lchown"),
  truncate: callback("truncate"),
  utimes: callback("utimes"),
  lutimes: callback("lutimes"),
  link: callback("link"),
  symlink: callback("symlink"),
  readlink: callback("readlink"),
  opendir: callback("opendir"),
  glob: callback("glob"),
  exists: callback("exists"),
};
