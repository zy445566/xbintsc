# xbintsc Node Extension Implemented Features

This document was compiled by checking the Node extension source
(`src/extensions/node/`) and the C runtime (`runtime/ext_node/`) file by file,
and lists the features and interfaces that the **Node extension actually
provides today**.

> Related documents:
> - Core language capabilities: [implemented.md](./implemented.md) / [unimplemented.md](./unimplemented.md)
> - Unimplemented parts of the Node extension: [node-unimplemented.md](./node-unimplemented.md)

> Language: **English** | [简体中文](./zh-CN/node-implemented.md)

---

## 1. Extension mechanism (implemented)

Location: `src/extensions/node/index.ts`, `src/extensions/node/module.ts`, `src/extensions/registry.ts`

- The Node extension plugs in through the uniform `Extension` object; its C sources are compiled and linked only when it is registered.
- Enable it via the CLI:

```bash
xbintsc run examples/node/read.ts --ext node
```

- Modular organisation: one subdirectory per Node module, in one-to-one correspondence with its C implementation:

```
src/extensions/node/           runtime/ext_node/
  index.ts    # nodeExtension      fs/read_file.c
  module.ts   # NodeModule iface   fs/write_file.c
  fs/index.ts                      fs/fs_ops.c
  fs/read-file.ts                  fs/fs_common.h
  fs/write-file.ts                 fs/promises.c
  fs/fs-ops.ts                     fs/streams.c
  fs/streams.ts                     fs/fd_ops.c
  path/index.ts                     fs/meta_ops.c
  os/index.ts                       fs/link_ops.c
  process/index.ts                  fs/copy_ops.c
  buffer/index.ts                   fs/dir.c
  stream/index.ts                   fs/glob.c
  net/index.ts                      fs/watch.c
  dgram/index.ts                    fs/constants.c
  http/index.ts                     path/path.c
  fs-promises/index.ts              os/os.c
  crypto/index.ts                   process/process.c
  url/index.ts                      buffer/buffer.c
  child_process/index.ts            stream/stream.c
  events/index.ts                   net/net.c
  util/index.ts                     dgram/dgram.c
  querystring/index.ts              http/http.c
  assert/index.ts                   node_common.h (event emitter / encoding helpers)
  test/index.ts                     crypto/crypto.c
  zlib/index.ts                     url/url.c
  stream-promises/index.ts          child_process/child_process.c
  worker_threads/index.ts           events/events.c
                                    util/util.c
                                    querystring/querystring.c
                                    assert/assert.c
                                    test/test.c
                                    zlib/zlib.c
                                    stream/pipeline.c
                                    worker_threads/worker_threads.c
```

- Core event loop: `runtime/xt_loop.c` (a `select(2)` reactor). The generated module's `main` calls `xt_run_event_loop()` after draining microtasks; it returns immediately when no fds are registered, so pure-computation programs are unaffected.

- The `NodeModule` interface:
  - `name`: the module name (e.g. `fs`)
  - `runtimeSources()`: the module's C sources
  - `builtins()`: global identifier → runtime symbol mapping (`fs` returns its exports here; `path` / `os` / `process` dispatch through namespaces, so they return an empty table)
  - `namespace` / `exports()`: the module's importable namespace and named exports (`import { join } from "path"`, `import path from "path"`)
- `resolveFrom(importMetaUrl, relative)`: resolves a path relative to the current module's directory into an absolute path, used to locate C sources.

---

## 2. The `fs` module (implemented, synchronous API only)

Location: `src/extensions/node/fs/*.ts`, `runtime/ext_node/fs/*.c`

### 2.1 Available functions (imported from `fs`)

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
| `watch(filename[, options][, listener])` | `xt_node_watch` | returns an emitter-shaped watcher (never fires; see §2.6) |
| `watchFile(filename[, options], listener)` | `xt_node_watch_file` | polling-shaped `StatWatcher` (never fires) |
| `unwatchFile(filename[, listener])` | `xt_node_unwatch_file` | stop watching |
| `constants` | `xt_fs_constants` | `F_OK` / `R_OK` / `W_OK` / `X_OK`, `COPYFILE_*`, `O_*`, `S_IF*` (host values) |
| `promises` | `xt_fs_promises` | the `fs/promises` facade (see §11) |

### 2.2 Encoding support

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

### 2.3 `statSync` return structure

Returns a plain object with numeric properties: `size`, `mode`, `uid`, `gid`, `dev`, `ino`, `nlink`, `rdev`, `blksize`, `blocks`, `atimeMs`, `mtimeMs`, `ctimeMs`, `birthtimeMs`.
Methods (native closures, callable): `isFile()`, `isDirectory()`, `isSymbolicLink()`, `isFIFO()`, `isSocket()`, `isBlockDevice()`, `isCharacterDevice()`.

### 2.4 How to call

`fs` exports are reached through an `import` (or the `node:fs` alias), not as bare
globals:

```ts
import { readFileSync, writeFileSync, existsSync } from "fs";

const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

### 2.5 Error handling

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

### 2.6 Deviations from Node

- **No asynchronous I/O scheduler** (the event loop is a `select(2)` reactor with
  no timers), so the callback-style `fs` functions (`readFile`, `writeFile`,
  `open`, …) are *not* provided; use `fs/promises` or the `*Sync` forms.
- `watch` / `watchFile` / `unwatchFile` return API-shaped emitter objects whose
  `.close()` / `.on()` methods exist but which **never emit** events.
- `Dir.read(cb)` / `Dir.close(cb)` invoke the callback **synchronously**.
- `readFileSync` returns a string by default instead of a `Buffer`.
- `utf16le` / `ucs2` are treated as raw bytes on read (no UTF-16 decoding).
- `mkdtempSync` appends 6 random characters to the prefix regardless of whether
  it ends in `XXXXXX`.
- `globSync` supports `*`, `?`, `[...]` and `**` but not the `exclude` callback
  or `follow`; `**` does not descend through symlinks (matching Node's default).
- `cpSync` is implemented on top of the synchronous helpers; symlinks are copied
  as symlinks unless `dereference: true` is set.
- **Windows**: `readlinkSync` raises `ENOSYS`; `chmodSync` / `lchmodSync` /
  `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` are no-ops;
  `statfsSync` returns zeroed fields.
- **macOS / Windows**: `lutimesSync` falls back to `utimesSync`.

---

## 3. The `path` module (implemented)

Location: `src/extensions/node/path/index.ts`, `runtime/ext_node/path/path.c`

Uses `path.<name>(...)` namespace calls, which the compiler lowers to
`xt_path_static(<name>, argc, argv)`. Results are produced with the POSIX `/`
separator on every platform (Windows accepts `/`), while inputs may use native
Windows separators: on Windows both `/` and `\` are recognised and drive
prefixes (`C:`) are preserved, so the self-hosted compiler resolves
drive-letter paths correctly.
Import the module as a namespace (`import path from "path"` / `import * as path
from "path"`) or pull in individual methods (`import { join } from "path"`).

| Method | Notes |
| --- | --- |
| `path.join(...parts)` | join and normalize |
| `path.resolve(...parts)` | resolve to an absolute path |
| `path.normalize(path)` | normalize |
| `path.dirname(path)` | directory name |
| `path.basename(path[, ext])` | file name, optionally without extension |
| `path.extname(path)` | extension |
| `path.isAbsolute(path)` | whether it is an absolute path |
| `path.relative(from, to)` | relative path |

```ts
import path from "path";
import { basename } from "path";

console.log(path.join("a", "b", "..", "c")); // a/c
console.log(basename("/x/y/z.txt"));          // z.txt
```

---

## 4. The `os` module (implemented)

Location: `src/extensions/node/os/index.ts`, `runtime/ext_node/os/os.c`

Uses `os.<name>(...)` namespace calls, lowered to `xt_os_static(<name>, argc, argv)`.
Import the module as a namespace (`import os from "os"`) or pull in individual
functions (`import { platform } from "os"`).

| Method | Notes |
| --- | --- |
| `os.platform()` | `darwin` / `linux` / `win32` / ... |
| `os.arch()` | `x64` / `arm64` / `ia32` / `arm` |
| `os.type()` | `Darwin` / `Linux` / `Windows_NT` / ... |
| `os.release()` | kernel version |
| `os.endianness()` | `LE` / `BE` |
| `os.homedir()` | user home directory |
| `os.tmpdir()` | temporary directory |
| `os.hostname()` | hostname |
| `os.totalmem()` / `os.freemem()` | total / free memory (bytes) |
| `os.cpus()` | array of CPU entries (`model` / `speed` placeholders) |

---

## 5. The `process` object (implemented)

Location: `src/extensions/node/process/index.ts`, `runtime/ext_node/process/process.c`

Method calls lower to `xt_process_call(<name>, argc, argv)`, property access to `xt_process_get(<name>)`.
Import the object (`import process from "process"`) to reach these.

| Method / property | Notes |
| --- | --- |
| `process.cwd()` | current working directory |
| `process.exit([code])` | exit the process |
| `process.uptime()` | process uptime (seconds) |
| `process.hrtime()` | `[seconds, nanoseconds]` array |
| `process.getuid()` | user ID (returns 0 on Windows) |
| `process.platform` / `process.arch` | platform / architecture |
| `process.pid` / `process.ppid` | process ID / parent process ID |
| `process.argv` | argument array (`argv[0]` is the executable) |
| `process.env` | environment variable object |
| `process.version` / `process.title` | placeholder strings |

> `argv` is captured by the generated `main` via `xt_set_program_args` and provided to the runtime.

---

## 6. The `buffer` module (implemented)

Location: `src/extensions/node/buffer/index.ts`, `runtime/ext_node/buffer/buffer.c`

xbintsc has no native binary value type, so `Buffer` is represented as a
**plain object**: each byte is a numeric property `"0".."n-1"`, plus a `length`
property, sharing the `xt_buffer_proto()` prototype that provides instance
methods. `xt_node_is_buffer` / `xt_node_buffer_bytes` let other modules access
bytes across modules.

| Static method | Notes |
| --- | --- |
| `Buffer.from(value[, encoding])` | construct from a string (hex / base64 / utf8), array or Buffer |
| `Buffer.alloc(size[, fill])` | allocate and fill |
| `Buffer.allocUnsafe(size)` | allocate |
| `Buffer.isBuffer(value)` | test |
| `Buffer.byteLength(value[, encoding])` | byte length |
| `Buffer.concat(list[, totalLength])` | concatenate |
| `Buffer.compare(a, b)` | compare |

Instance methods: `toString([encoding])`, `toJSON()`, `slice(start, end)`, `subarray(...)`, `equals(other)`, `compare(other)`, `copy(target[, targetStart, sourceStart, sourceEnd])`, `write(string[, offset[, length[, encoding]]])`, `fill(value)`, `reverse()`, `indexOf(value)`, `lastIndexOf(value)`, `includes(value)`, `keys()`, `values()`, plus `readUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`, `writeUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`.

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```

---

## 7. The `stream` module (implemented)

Location: `src/extensions/node/stream/index.ts`, `runtime/ext_node/stream/stream.c`

`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` are used as
**global constructors** (`new Readable()` etc.); statics such as
`stream.Readable.from(...)` are resolved through the `stream` namespace. Streams
are EventEmitters with a **synchronous event model**: `on('data')` flushes the
`push` buffer, and `write` delivers immediately.

| Method | Notes |
| --- | --- |
| `push(chunk)` / `read([n])` | Readable side |
| `write(chunk)` / `end([chunk])` | Writable side |
| `pipe(destination)` | forward data |
| `on('data' / 'end' / 'finish')` | events |
| `pause()` / `resume()` / `setEncoding(enc)` / `destroy()` | flow control |

---

## 8. The `net` module (implemented)

Location: `src/extensions/node/net/index.ts`, `runtime/ext_node/net/net.c`

TCP server and client, built on the core event loop.

| API | Notes |
| --- | --- |
| `net.createServer([connectionListener])` | create a TCP server (equivalent to the `Server` constructor) |
| `net.connect(...)` / `net.createConnection(...)` | connect (blocking connect, then registers with the event loop) |
| `net.isIP(s)` / `net.isIPv4(s)` / `net.isIPv6(s)` | address test |

`Server`: `listen(port[, host][, cb])`, `close([cb])`, `address()`, `getConnections(cb)`, events `listening` / `connection` / `close`.

`Socket`: `write(data[, cb])`, `end([data])`, `destroy()`, `address()`, `setEncoding(enc)`, `pause()` / `resume()`, events `data` / `end` / `close` / `connect` / `error`.

---

## 9. The `dgram` module (implemented)

Location: `src/extensions/node/dgram/index.ts`, `runtime/ext_node/dgram/dgram.c`

UDP sockets. `dgram.createSocket(type | options[, cb])` returns an EventEmitter.

| Method | Notes |
| --- | --- |
| `bind([port][, address][, cb])` | bind (`send` auto-binds if unbound) |
| `send(msg[, offset, length,] port[, address][, cb])` | send a datagram |
| `close([cb])` / `address()` | close / query address |
| `setBroadcast(b)` / `setTTL(n)` / `setMulticastTTL(n)` | socket options |
| `on('message', (msg, rinfo) => ...)` | receive a datagram; `rinfo` has `address` / `port` / `family` / `size` |

---

## 10. The `http` module (implemented)

Location: `src/extensions/node/http/index.ts`, `runtime/ext_node/http/http.c`

The server wraps a `net` server: each connection accumulates bytes until a
complete request (request line + headers + `Content-Length` body) is available,
then invokes the `request` listener with `req`/`res`. The client wraps a `net`
socket, writes an HTTP/1.1 request and parses the response after the connection
closes.

| API | Notes |
| --- | --- |
| `http.createServer([requestListener])` | create an HTTP server |
| `http.request(options[, cb])` | create a `ClientRequest` (`write` / `end` / `setHeader`) |
| `http.get(url[, cb])` | issue a GET |

`IncomingMessage` (`req` / response): `method`, `url`, `httpVersion`, `headers`, `statusCode`, `data` / `end` events, `setEncoding`.

`ServerResponse` (`res`): `writeHead(status[, message][, headers])`, `setHeader` / `getHeader` / `removeHeader` / `getHeaders`, `write(chunk)`, `end([chunk])`, events `finish` / `close`. Responses always carry `Connection: close` (no keep-alive).

---

## 11. The `fs/promises` module (implemented)

Location: `src/extensions/node/fs-promises/index.ts`, `runtime/ext_node/fs/promises.c`

There is no asynchronous I/O scheduler, so each function wraps the corresponding
synchronous `fs` implementation in an **already-settled Promise**. Failures
**reject** with the same Node-shaped `Error` (with `code` / `errno` / `syscall` /
`path`) that the synchronous form throws. `fs.promises` is also reachable from
the `fs` module (`import { promises as fsp } from "fs"`), and both namespaces
expose a `constants` object.

Promise exports: `readFile`, `writeFile`, `appendFile`, `mkdir`, `readdir`,
`rm`, `unlink`, `rmdir`, `rename`, `copyFile`, `cp`, `realpath`, `stat`,
`lstat`, `statfs`, `access`, `open`, `chmod`, `lchmod`, `chown`, `lchown`,
`truncate`, `utimes`, `lutimes`, `link`, `symlink`, `readlink`, `mkdtemp`,
`opendir`, `glob`, `watch`.

`open(...)` resolves to a **`FileHandle`** with:
`read`, `write`, `readFile`, `writeFile`, `appendFile`, `close`, `stat`,
`truncate`, `chmod`, `chown`, `utimes`, `sync`, `datasync`.

```ts
import { readFile, writeFile, open } from "fs/promises";

async function main(): Promise<void> {
  await writeFile("/tmp/a.txt", "hi");
  console.log(await readFile("/tmp/a.txt"));
  const handle = await open("/tmp/a.txt", "r");
  console.log(await handle.readFile("utf8"));
  await handle.close();
}
main();
```

### 11.1 Deviations from Node

- Because I/O is synchronous, the Promise settles before the returned value is
  awaited (the event loop does not yield).
- `FileHandle.appendFile` behaves like `writeFile` (it writes at the current
  file position rather than appending).
- `FileHandle.readFile()` reads from the current file-descriptor offset.
- `Dir.read` / `Dir.close` (both the sync and promise/callback forms) complete
  immediately.
- `watch` resolves to the same never-emitting watcher object as `fs.watch`.

---

## 12. The `child_process` module (implemented)

Location: `src/extensions/node/child_process/index.ts`, `runtime/ext_node/child_process/child_process.c`

`spawnSync(command, args[, options])` runs a program to completion and returns
`{ status, stdout, stderr }`. `options.cwd` sets the working directory and
`options.stdio: "inherit"` hands the child the parent's stdout/stderr (used by
`xbintsc run`, so a compiled program's output streams live); otherwise
stdout/stderr are captured as UTF-8 strings.

| Option | Notes |
| --- | --- |
| `cwd` | working directory for the child |
| `stdio: "inherit"` | share the parent's stdout/stderr instead of capturing |
| `encoding` | accepted and ignored (output is always decoded as UTF-8) |

```ts
import { spawnSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);
```

---

## 13. The `events` module (implemented)

Location: `src/extensions/node/events/index.ts`, `runtime/ext_node/events/events.c`

Provides a standalone `EventEmitter`, available both as a **global constructor**
(`new EventEmitter()`) and as a named export
(`import { EventEmitter } from "events"`). Instances share the runtime emitter
used by `stream` / `net` / `http` (listeners live in an internal `__xt_events`
property), with the fuller `events` surface layered on top:

| Method | Notes |
| --- | --- |
| `on(name, fn)` / `addListener(name, fn)` | append a listener |
| `once(name, fn)` | fire at most once, then remove itself |
| `prependListener(name, fn)` / `prependOnceListener(name, fn)` | insert at the front |
| `off(name, fn)` / `removeListener(name, fn)` | remove a listener |
| `removeAllListeners([name])` | clear one event (or all events) |
| `emit(name[, ...args])` | invoke listeners |
| `listeners(name)` / `rawListeners(name)` | listener array |
| `listenerCount(name)` | number of listeners |
| `eventNames()` | names that currently have listeners |
| `setMaxListeners(n)` / `getMaxListeners()` | bookkeeping (default 10) |

Statics reachable through the namespace (`import ee from "events"`):
`listenerCount`, `getEventListeners`, `getMaxListeners`, `setMaxListeners`,
`once`, `addAbortListener`.

```ts
import { EventEmitter } from "events";

const em = new EventEmitter();
em.once("ready", () => console.log("ready"));
em.emit("ready"); // ready
em.emit("ready"); // nothing: the listener already ran
```

---

## 14. The `util` module (implemented)

Location: `src/extensions/node/util/index.ts`, `runtime/ext_node/util/util.c`

Both named imports (`import { format } from "util"`) and namespace calls
(`import util from "util"` / `import * as util from "util"`) are supported.

| Function | Notes |
| --- | --- |
| `format(fmt, ...args)` | `%s` `%d` `%i` `%f` `%j` `%o` `%O` `%c` `%%` placeholders |
| `formatWithOptions(opts, fmt, ...args)` | options accepted and ignored |
| `inspect(value)` | recursive printer (depth-limited, strings quoted) |
| `isDeepStrictEqual(a, b)` | structural comparison (`NaN` equals `NaN`) |
| `inherits(ctor, superCtor)` | prototype wiring |
| `deprecate(fn, msg)` | returns `fn` unchanged (there is no warning channel) |
| `promisify(fn)` | wraps a trailing-callback function into a `Promise` |
| `isString` `isNumber` `isBoolean` `isUndefined` `isNull` `isFunction` `isArray` `isObject` `isBuffer` `isDate` `isRegExp` `isPromise` `isError` | type predicates |

```ts
import { format, promisify } from "util";

console.log(format("%s=%d", "n", 3)); // n=3
```

---

## 15. The `querystring` module (implemented)

Location: `src/extensions/node/querystring/index.ts`, `runtime/ext_node/querystring/querystring.c`

| Function | Notes |
| --- | --- |
| `parse(str[, sep[, eq]])` / `decode` | parse into an object; repeated keys become arrays |
| `stringify(obj[, sep[, eq]])` / `encode` | serialize; spaces become `+`, arrays repeat the key |
| `escape(str)` / `unescape(str)` | percent-encode / decode (`+` decodes to a space) |

Defaults: `sep = "&"`, `eq = "="`.

```ts
import { parse, stringify } from "querystring";

const q = parse("a=1&b=2&b=3");      // { a: "1", b: ["2", "3"] }
console.log(stringify({ x: "a b" })); // x=a+b
```

---

## 16. Implemented Node capabilities quick reference

| Category | Contents |
| --- | --- |
| Extension registration | `nodeExtension` (`--ext node`), `NodeModule` interface, `resolveFrom` utility |
| fs read | `readFileSync`, `readTextFile` (`xt_node_read_text_file`), supports hex / base64 / base64url |
| fs write | `writeFileSync`, `appendFileSync`, supports hex / base64 / base64url; string or `Buffer` |
| fs directories | `readdirSync` (`withFileTypes` / `recursive`), `mkdirSync`, `rmSync`, `unlinkSync`, `rmdirSync`, `opendirSync` (`Dir`), `globSync`, `mkdtempSync` |
| fs other | `existsSync`, `renameSync`, `copyFileSync`, `cpSync`, `realpathSync`, `statSync`, `lstatSync`, `statfsSync`, `accessSync`, `chmodSync`, `chownSync`, `lchmodSync`, `lchownSync`, `truncateSync`, `utimesSync`, `lutimesSync`, `linkSync`, `symlinkSync`, `readlinkSync`, `watch`, `watchFile`, `unwatchFile` |
| fs descriptors | `openSync`, `closeSync`, `readSync`, `writeSync`, `readvSync`, `writevSync`, `fstatSync`, `fsyncSync`, `fdatasyncSync`, `ftruncateSync`, `fchmodSync`, `fchownSync`, `futimesSync` |
| fs constants | `constants` (`F_OK`, `R_OK`, `W_OK`, `X_OK`, `COPYFILE_*`, `O_*`, `S_IF*`) |
| path | `join` `resolve` `normalize` `dirname` `basename` `extname` `isAbsolute` `relative` |
| os | `platform` `arch` `type` `release` `endianness` `homedir` `tmpdir` `hostname` `totalmem` `freemem` `cpus` |
| process | `cwd` `exit` `uptime` `hrtime` `getuid`; `platform` `arch` `pid` `ppid` `argv` `env` `version` `title` |
| buffer | `Buffer.from/alloc/allocUnsafe/isBuffer/byteLength/concat/compare`; instances `toString/toJSON/slice/.../readUInt32BE/writeUInt32BE` |
| stream | `Readable` `Writable` `Duplex` `Transform` `PassThrough`; `push/read/write/end/pipe/on` |
| net | `createServer` `connect` `createConnection` `isIP/isIPv4/isIPv6`; `Server` `Socket` |
| dgram | `createSocket`; `bind/send/close/address/setBroadcast/setTTL` |
| http | `createServer` `request` `get`; `ClientRequest`, `IncomingMessage`, `ServerResponse` |
| child_process | `spawnSync(command, args[, {cwd, stdio}])` returning `status` / `stdout` / `stderr` |
| assert | `ok/equal/notEqual/strictEqual/notStrictEqual/deepStrictEqual/notDeepStrictEqual/throws/doesNotThrow/ifError/match/doesNotMatch/fail` (`import assert from "node:assert"`) |
| test | `test(name, fn)` / `it` / `describe` / `skip` / `todo` (`import test from "node:test"`), TAP output, non-zero exit on failure |
| zlib | `createGzip()` (consumed by `stream/promises` pipeline) |
| stream/promises | `pipeline(...)` (synchronous drain, returns a resolved Promise) |
| worker_threads | `Worker`, `isMainThread`, `workerData`, `parentPort` (re-executes the binary as a child process) |
| events | `EventEmitter` (global + named); `on/once/off/emit/listeners/listenerCount/eventNames`; statics `listenerCount/getEventListeners/getMaxListeners/setMaxListeners/once/addAbortListener` |
| util | `format` `formatWithOptions` `inspect` `isDeepStrictEqual` `inherits` `deprecate` `promisify`; `isString/isNumber/isBoolean/isUndefined/isNull/isFunction/isArray/isObject/isBuffer/isDate/isRegExp/isPromise/isError` |
| querystring | `parse`/`decode` `stringify`/`encode` `escape` `unescape` |
| crypto | `createHash(algorithm)` with `update`/`digest` and the streaming API (`setEncoding`/`write`/`end`/`read`); SHA-1 and SHA-256 |
| globals | `btoa` / `atob` base64 helpers |
| url | `pathToFileURL` `fileURLToPath` |
| fs/promises | `readFile` `writeFile` `appendFile` `mkdir` `readdir` `rm` `unlink` `rmdir` `rename` `copyFile` `cp` `realpath` `stat` `lstat` `statfs` `access` `open` `chmod` `lchmod` `chown` `lchown` `truncate` `utimes` `lutimes` `link` `symlink` `readlink` `mkdtemp` `opendir` `glob` `watch` `constants`; `FileHandle` |
| Event loop | `xt_loop` (`select` reactor), `xt_run_event_loop()`, `xt_loop_add/update/remove` |
| Calling convention | uniform `(argc, argv)` ABI, returns `xt_value` |
| Linking | after registration, compiles `runtime/ext_node/**` and links it with the runtime |
