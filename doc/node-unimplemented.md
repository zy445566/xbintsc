# xbintsc Node Extension Unimplemented Features

This document was compiled by checking the Node extension source
(`src/extensions/node/`) and the C runtime (`runtime/ext_node/`) file by file,
and lists the features that the **Node extension currently does not implement /
is missing**.

> Related documents:
> - Core language capabilities: [implemented.md](./implemented.md) / [unimplemented.md](./unimplemented.md)
> - Implemented parts of the Node extension: [node-implemented.md](./node-implemented.md)

> Language: **English** | [简体中文](./zh-CN/node-unimplemented.md)

---

## 1. Unimplemented in the `fs` module

`fs` currently implements synchronous read/write, directory operations and
`statSync`, and `fs/promises` is provided as a "wrapper over the synchronous
implementation" (see [node-implemented.md](./node-implemented.md)); the rest is
still unimplemented:

| Unimplemented API | Category |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` | async callback-style read/write (`fs/promises` provides a Promise version) |
| `watch` / `watchFile` | file watching |
| `openSync` / `closeSync` / `readSync` / `writeSync` | file-descriptor-level operations |
| `mkdtempSync` | temporary directory creation |
| `linkSync` / `symlinkSync` / `readlinkSync` | hard links / symlinks |
| `truncateSync` / `chmodSync` / `chownSync` / `utimesSync` | metadata modification |
| `createReadStream` / `createWriteStream` | streaming read/write |

### 1.1 Read semantics gaps

- `readFileSync` supports encoding options (default/`utf8`/`ascii`/`latin1`/`binary`/`hex`/`base64`), but **does not return a `Buffer`**: even though `Buffer` exists (simulated as a plain object), `readFileSync` still returns a string.
- The error handling of `fs/promises` differs from Node: a failure in the underlying synchronous implementation only prints to stderr and resolves to `undefined`, so the **Promise does not reject** (xbintsc does not yet have a catchable exception system).
- Error handling differs from Node: on open failure it only prints to stderr and returns `undefined`, and does not throw `Error` / `ENOENT` exceptions (xbintsc does not yet have a catchable exception system).

---

## 2. Other Node modules fully unimplemented

The following Node built-in modules have no corresponding extension / builtin
(`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` / `net` /
`dgram` / `http` are implemented, see [node-implemented.md](./node-implemented.md)):

| Module | Notes |
| --- | --- |
| `crypto` | hashing, random numbers, encryption |
| `https` | TLS version of HTTP |
| `child_process` | `spawn` `exec` `fork` |
| `util` | `inspect` `format` `promisify` etc. |
| `events` | standalone `EventEmitter` class (streams/sockets already have emitter capabilities internally, but there is no `events` module / global `EventEmitter`) |
| `url` | URL parsing |
| `querystring` | query string parsing |
| `zlib` | compression / decompression |
| `readline` | command-line reading |
| `worker_threads` | worker threads |
| `tls` / `cluster` / `vm` / `os` (partially) etc. | other modules not listed |

---

## 3. Node global objects / namespaces

| Unimplemented | Notes |
| --- | --- |
| `global` / `globalThis` | none |
| `__dirname` / `__filename` | none |
| `require` / `module` / `exports` | none (xbintsc has no CommonJS module runtime) |
| `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask` | no timers |
| `fs.readFileSync(...)` namespace-style calls | ✗ unsupported. `fs` builtins can only be called as bare global identifiers, e.g. `readFileSync(...)`; the same applies to `fs/promises` |
| `import { readFileSync } from "fs"` | ✗ cannot run. The `import` statement itself reports `UnsupportedFeature` at code generation (see [unimplemented.md](./unimplemented.md)); extension builtins are global symbol mappings, unrelated to import |

Supported namespace calls: `path.*`, `os.*`, `process.*` (methods), `Buffer.*`,
`stream.*`, `net.*`, `dgram.*`, `http.*`, plus properties such as
`process.platform` / `process.argv`. `Readable` / `Writable` / `Duplex` /
`Transform` / `PassThrough` / `Buffer` can also be used as global constructors.

---

## 4. Async / event loop model

Implemented:

- Core event loop `runtime/xt_loop.c` (a `select(2)` reactor); `net` / `dgram` / `http` are all built on it.
- `Promise`, `async` / `await` are available (see the core documents); `fs/promises` returns Promises.
- Event emitters (`on` / `addListener` / `once` / `off` / `removeListener` / `emit`) are provided on streams and sockets.

Still unimplemented:

- Timers `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask`.
- A real asynchronous I/O scheduler: `fs/promises` is essentially an immediate-settle wrapper around synchronous operations and does not yield while waiting for I/O.
- `once` is currently equivalent to `on` (no "remove after firing once" semantics).
- `net`'s `connect` is blocking; HTTP responses require `Connection: close`, with no keep-alive / chunked transfer / pipelining.

---

## 5. Bun extension unimplemented

The `Bun.file` and other Bun APIs mentioned in the design document are only
design examples of the extension mechanism and are **not yet implemented**:

- No `bun` extension directory, no Bun builtins, no Bun C runtime.
- Under `src/extensions/` there is currently only the single platform extension `node` (plus the core extension).

---

## 6. Quick reference: Node extension unimplemented list

```
Implemented (fs): readFileSync (with encoding), readTextFile, writeFileSync, appendFileSync,
                  existsSync, readdirSync, mkdirSync, rmSync, unlinkSync, rmdirSync,
                  renameSync, copyFileSync, realpathSync, statSync, lstatSync
Implemented (fs/promises): readFile, writeFile, appendFile, mkdir, readdir, rm, unlink,
                           rmdir, rename, copyFile, realpath, stat, lstat, access
Implemented (other modules): path (join/resolve/normalize/dirname/basename/extname/isAbsolute/relative),
                             os (platform/arch/type/release/endianness/homedir/tmpdir/hostname/totalmem/freemem/cpus),
                             process (cwd/exit/uptime/hrtime/getuid/platform/arch/pid/ppid/argv/env/version/title),
                             buffer (from/alloc/isBuffer/byteLength/concat/compare + instance methods),
                             stream (Readable/Writable/Duplex/Transform/PassThrough, pipe),
                             net (createServer/connect/isIP + Server/Socket),
                             dgram (createSocket + bind/send/close/address),
                             http (createServer/request/get + req/res)

Unimplemented (fs): readFile, writeFile, appendFile (async callback-style), watch/watchFile,
                    open/read/write/close, mkdtempSync, link/symlink/readlink,
                    chmod/chown/utimes/truncate, createReadStream/createWriteStream, Buffer return

Unimplemented (other modules): crypto, https, child_process, util, events (standalone class), url,
                               querystring, zlib, readline, worker_threads, tls, cluster, vm

Unimplemented (global/namespace): global/globalThis, __dirname, __filename,
                                  require/module/exports, fs.readFileSync(...) namespace calls

Unimplemented (async): timers setTimeout / setInterval / setImmediate / queueMicrotask,
                       real async I/O scheduling, once "fire once" semantics, keep-alive

Unimplemented (platform): Bun extension (design example only)
```
