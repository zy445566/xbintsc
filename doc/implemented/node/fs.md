# `fs`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/fs.md)

Location: `src/extensions/node/fs/*.ts`, `runtime/ext_node/fs/*.c`

## Available functions (imported from `fs`)

| Imported function | Runtime symbol | Notes |
| --- | --- | --- |
| `readFileSync(path[, options])` | `xt_node_read_text_file` | synchronously read a file, returns a string; supports encoding options |
| `readTextFile(path)` | `xt_node_read_text_file` | alias of `readFileSync` (same symbol) |
| `writeFileSync(path, data[, options])` | `xt_node_write_file` | overwrite write; accepts a string or a `Buffer` |
| `appendFileSync(path, data[, options])` | `xt_node_append_file` | append write; accepts a string or a `Buffer` |
| `existsSync(path)` | `xt_node_exists` | whether it exists, returns a boolean |
| `readdirSync(path[, options])` | `xt_node_read_dir` | directory entry names; `{ withFileTypes: true }` returns `Dirent`s, `{ recursive: true }` recurses |
| `mkdirSync(path[, options])` | `xt_node_mkdir` | create a directory; `{ recursive: true }` creates recursively, `{ mode }` is honoured |
| `rmSync(path[, options])` | `xt_node_rm` | delete a file/directory; `{ recursive: true }` deletes recursively |
| `rmdirSync(path[, options])` | `xt_node_rmdir` | delete an empty directory |
| `unlinkSync(path)` | `xt_node_unlink` | delete a file |
| `renameSync(oldPath, newPath)` | `xt_node_rename` | rename / move |
| `copyFileSync(src, dest[, flags])` | `xt_node_copy_file` | copy a file; honours `COPYFILE_EXCL` |
| `cpSync(src, dest[, options])` | `xt_node_cp` | recursive copy; `recursive`, `force`, `errorOnExist`, `dereference`, `preserveTimestamps` |
| `realpathSync(path)` | `xt_node_realpath` | resolve to an absolute path |
| `statSync(path)` | `xt_node_stat` | file metadata object (follows symlinks) |
| `lstatSync(path)` | `xt_node_lstat` | file metadata object (does not follow symlinks) |
| `statfsSync(path)` | `xt_node_statfs` | filesystem statistics (`bsize`, `blocks`, `bfree`, …) |
| `accessSync(path[, mode])` | `xt_node_access` | check accessibility |
| `chmodSync(path, mode)` | `xt_node_chmod` | change permissions |
| `lchmodSync(path, mode)` | `xt_node_lchmod` | change symlink permissions |
| `chownSync(path, uid, gid)` | `xt_node_chown` | change ownership |
| `lchownSync(path, uid, gid)` | `xt_node_lchown` | change symlink ownership |
| `truncateSync(path[, len])` | `xt_node_truncate` | truncate a file |
| `utimesSync(path, atime, mtime)` | `xt_node_utimes` | set access/modification time (number of seconds or a `Date`) |
| `lutimesSync(path, atime, mtime)` | `xt_node_lutimes` | set symlink times |
| `mkdtempSync(prefix)` | `xt_node_mkdtemp` | create a unique temporary directory |
| `linkSync(existing, newPath)` | `xt_node_link` | hard link |
| `symlinkSync(target, path[, type])` | `xt_node_symlink` | symbolic link |
| `readlinkSync(path)` | `xt_node_readlink` | read a symbolic link target |
| `opendirSync(path[, options])` | `xt_node_opendir` | returns a `Dir` (`readSync` / `closeSync` / `read` / `close`) |
| `globSync(pattern[, options])` | `xt_node_glob` | glob matching (`*`, `?`, `[...]`, `**`), `{ cwd, withFileTypes }` |
| `openSync(path[, flags[, mode]])` | `xt_node_open` | open a file descriptor |
| `closeSync(fd)` | `xt_node_close` | close a descriptor |
| `readSync(fd, buffer, offset, length, position)` | `xt_node_read` | read into a `Buffer` |
| `writeSync(fd, data[, offset[, length[, position]]])` | `xt_node_write` | write a string / `Buffer` |
| `readvSync(fd, buffers[, position])` | `xt_node_readv` | scatter read |
| `writevSync(fd, buffers[, position])` | `xt_node_writev` | gather write |
| `fstatSync(fd)` | `xt_node_fstat` | `stat` for a descriptor |
| `fsyncSync(fd)` / `fdatasyncSync(fd)` | `xt_node_fsync` / `xt_node_fdatasync` | flush a descriptor |
| `ftruncateSync(fd[, len])` | `xt_node_ftruncate` | truncate a descriptor |
| `fchmodSync(fd, mode)` | `xt_node_fchmod` | `chmod` for a descriptor |
| `fchownSync(fd, uid, gid)` | `xt_node_fchown` | `chown` for a descriptor |
| `futimesSync(fd, atime, mtime)` | `xt_node_futimes` | `utimes` for a descriptor |
| `watch(filename[, options][, listener])` | `xt_node_watch` | returns an emitter-shaped watcher (never fires; see [Unimplemented: `fs`](../../unimplemented/node/fs.md)) |
| `watchFile(filename[, options], listener)` | `xt_node_watch_file` | polling-shaped `StatWatcher` (never fires) |
| `unwatchFile(filename[, listener])` | `xt_node_unwatch_file` | stop watching |
| `constants` | `xt_fs_constants` | `F_OK` / `R_OK` / `W_OK` / `X_OK`, `COPYFILE_*`, `O_*`, `S_IF*` (host values) |
| `promises` | `xt_fs_promises` | the `fs/promises` facade (see [`fs/promises`](./fs-promises.md)) |

## Encoding support

The `options` of `readFileSync` / `writeFileSync` / `appendFileSync` may be an
encoding string or `{ encoding: "..." }`:

| Encoding | Read | Write |
| --- | --- | --- |
| default / `utf8` / `utf-8` / `ascii` / `latin1` / `binary` | raw UTF-8 text | write as text bytes |
| `hex` | lowercase hex string | parse hex then write |
| `base64` | Base64 string | parse Base64 then write |
| `base64url` | Base64url string | parse Base64url then write |
| `utf16le` / `ucs2` | raw bytes read as text (no UTF-16 decoding) | write as UTF-8 text |

> `readFileSync` still returns a **string** by default rather than a `Buffer`,
> even though the `Buffer` class exists, so that `console.log(readFileSync(p))`
> keeps printing text.

## `statSync` return structure

Returns a plain object with numeric properties: `size`, `mode`, `uid`, `gid`, `dev`, `ino`, `nlink`, `rdev`, `blksize`, `blocks`, `atimeMs`, `mtimeMs`, `ctimeMs`, `birthtimeMs`.
Methods (native closures, callable): `isFile()`, `isDirectory()`, `isSymbolicLink()`, `isFIFO()`, `isSocket()`, `isBlockDevice()`, `isCharacterDevice()`.

## How to call

`fs` exports are reached through an `import` (or the `node:fs` alias), not as bare
globals:

```ts
import { readFileSync, writeFileSync, existsSync } from "fs";

const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

## Error handling

On failure the functions **throw** a Node-shaped `Error` object carrying
`name` (`Error`), `message`, `code` (e.g. `ENOENT`), `errno` (the numeric
`errno`), `syscall` and `path`. `existsSync` still returns a boolean and never
throws.

```ts
import { readFileSync } from "fs";
try {
  readFileSync("/does/not/exist");
} catch (error) {
  console.log((error as any).code); // ENOENT
}
```


Known deviations are listed in [Unimplemented: `fs`](../../unimplemented/node/fs.md).
