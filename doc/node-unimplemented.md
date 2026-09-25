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

`fs` now implements the **synchronous** API surface (files, directories,
descriptors, metadata, links, `cp`, `glob`, `Dir`, `constants`) and
`fs/promises` wraps each synchronous function in an already-settled Promise
(see [node-implemented.md](./node-implemented.md)). The rest is still
unimplemented because xbintsc has **no asynchronous I/O scheduler / event
loop**:

| Unimplemented API | Category |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` / `open` / `close` / `read` / `write` / `stat` / … | callback-style async file operations (`fs/promises` provides the Promise forms) |
| `createReadStream` / `createWriteStream` | streaming read/write |
| `watch` / `watchFile` | **real** file watching: the functions exist but the returned objects never emit (the event loop has no timers/inotify backend) |
| `openAsBlob`, `statfs` callback form, `rmdir` `maxRetries` / `retryDelay` | misc. options requiring async retry |
| `glob` `exclude` / `follow`, `cp` `filter` | option callbacks |

### 1.1 Read semantics gaps

- `readFileSync` supports encoding options (default/`utf8`/`ascii`/`latin1`/`binary`/ `hex`/`base64`/`base64url`), but **does not return a `Buffer`** by default: even though `Buffer` exists (simulated as a plain object), `readFileSync` still returns a string. Pass a `Buffer`-producing path or read raw bytes explicitly.
- `utf16le` / `ucs2` decoding is not implemented (the bytes are returned as text).
- `watch` / `watchFile` / `unwatchFile` return API-shaped emitter objects whose `.close()` / `.on()` methods exist but which **never fire** events.
- `Dir.read(cb)` / `Dir.close(cb)` and the async form of `FileHandle` methods complete **synchronously** (the callback/Promise is invoked immediately).
- `mkdtempSync` always appends 6 random characters to the prefix (it does not require a trailing `XXXXXX`).
- `globSync` supports `*`, `?`, `[...]` and `**` but not the `exclude` callback or `follow`; `**` does not descend through symlinks (matching Node's default).
- `cpSync` symlink handling follows Node for the common cases but `verbatimSymlinks` is not supported.
- Platform deviations: on Windows `readlinkSync` raises `ENOSYS` and `chmodSync` / `lchmodSync` / `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` are no-ops; `statfsSync` returns zeroed fields on Windows/AIX/Sun; `lutimesSync` falls back to `utimesSync` on macOS/Windows.

---

## 2. Other Node modules fully unimplemented

The following Node built-in modules have no corresponding extension / builtin
(`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` /
`dgram` / `http` / `events` / `util` / `querystring` / `assert` / `test` are
implemented, and `crypto` / `url` / `child_process` / `zlib` /
`stream/promises` / `worker_threads` are partially implemented; see
[node-implemented.md](./node-implemented.md)):

| Module | Notes |
| --- | --- |
| `https` | TLS version of HTTP |
| `readline` | command-line reading |
| `tls` / `cluster` / `vm` / `os` (partially) etc. | other modules not listed |

> `crypto` (only `createHash`, now with the streaming API), `url` (only
> `pathToFileURL` / `fileURLToPath`), `child_process` (only `spawnSync`),
> `zlib` (only `createGzip`) and `worker_threads` (only `Worker` /
> `isMainThread` / `workerData` / `parentPort`) are partially implemented.

---

## 3. Node global objects / namespaces

| Unimplemented | Notes |
| --- | --- |
| `global` / `globalThis` | none |
| `__dirname` / `__filename` | none |
| `require` / `module` / `exports` | none (xbintsc has no CommonJS module runtime) |
| `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask` | no timers |
| `fs.readFileSync(...)` without an `import` | ✗ unsupported. `fs` is not a global; import it first (`import fs from "fs"` or `import * as fs from "node:fs"`) and `fs.readFileSync(...)` lowers to the module's runtime symbol. |

Node modules are reached through `import` with a bare or `node:`-prefixed
specifier (`import { readFileSync } from "fs"`, `import path from "path"`,
`import { platform } from "node:os"`). Named and namespace imports both resolve
to the extension module's runtime entries.

Supported namespace calls: `path.*`, `os.*`, `process.*`, `fs.*`,
`fs/promises.*` (methods), `child_process.*`, `crypto.*`, `url.*` once imported
(a namespace/default import or, for `path`/`os`/`process`, the global name),
plus the dispatcher-backed namespaces `Buffer.*`, `stream.*`, `net.*`,
`dgram.*`, `http.*` and properties such as `process.platform` /
`process.argv`. `Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough`
/ `Buffer` can also be used as global constructors.

---

## 4. Async / event loop model

Implemented:

- Core event loop `runtime/xt_loop.c` (a `select(2)` reactor); `net` / `dgram` / `http` are all built on it.
- `Promise`, `async` / `await` are available (see the core documents); `fs/promises` returns Promises.
- A standalone `EventEmitter` is provided by the `events` module (also a global constructor); streams and sockets additionally carry internal emitter methods (`on` / `addListener` / `once` / `off` / `removeListener` / `emit`).

Still unimplemented:

- Timers `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask`.
- A real asynchronous I/O scheduler: `fs/promises` is essentially an immediate-settle wrapper around synchronous operations and does not yield while waiting for I/O.
- On streams and sockets the built-in `once` is equivalent to `on` (no "remove after firing once" semantics); the `events` module's `EventEmitter` does implement real `once`.
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
Implemented (fs): the full synchronous surface — readFileSync (with encoding), readTextFile,
                  writeFileSync, appendFileSync, existsSync, readdirSync (withFileTypes/recursive),
                  mkdirSync, rmSync, unlinkSync, rmdirSync, renameSync, copyFileSync, cpSync,
                  realpathSync, statSync, lstatSync, statfsSync, accessSync, chmodSync, lchmodSync,
                  chownSync, lchownSync, truncateSync, utimesSync, lutimesSync, mkdtempSync,
                  linkSync, symlinkSync, readlinkSync, opendirSync (Dir), globSync, watch,
                  watchFile, unwatchFile, constants,
                  openSync, closeSync, readSync, writeSync, readvSync, writevSync, fstatSync,
                  fsyncSync, fdatasyncSync, ftruncateSync, fchmodSync, fchownSync, futimesSync
Implemented (fs/promises): readFile, writeFile, appendFile, mkdir, readdir, rm, unlink,
                           rmdir, rename, copyFile, cp, realpath, stat, lstat, statfs, access,
                           open (FileHandle), chmod, lchmod, chown, lchown, truncate, utimes,
                           lutimes, link, symlink, readlink, mkdtemp, opendir, glob, watch, constants
Implemented (other modules): path (join/resolve/normalize/dirname/basename/extname/isAbsolute/relative),
                             os (platform/arch/type/release/endianness/homedir/tmpdir/hostname/totalmem/freemem/cpus),
                             process (cwd/exit/uptime/hrtime/getuid/platform/arch/pid/ppid/argv/env/version/title),
                             buffer (from/alloc/isBuffer/byteLength/concat/compare + instance methods),
                             stream (Readable/Writable/Duplex/Transform/PassThrough, pipe),
                             net (createServer/connect/isIP + Server/Socket),
                             dgram (createSocket + bind/send/close/address),
                             http (createServer/request/get + req/res),
                             events (EventEmitter: on/once/off/emit/listeners/listenerCount/eventNames),
                             util (format/inspect/isDeepStrictEqual/inherits/promisify + isX),
                             querystring (parse/stringify/escape/unescape),
                             crypto (createHash + streaming, SHA-1/SHA-256),
                             url (pathToFileURL/fileURLToPath only),
                             child_process (spawnSync only),
                             zlib (createGzip only), stream/promises (pipeline only),
                             worker_threads (Worker/isMainThread/workerData/parentPort only)

Unimplemented (fs): callback-style async file operations (readFile/writeFile/appendFile/open/
                    read/write/close), createReadStream/createWriteStream, real file watching
                    (watch/watchFile objects never emit),
                    glob exclude/follow, cp filter, utf16le decoding, Buffer return from readFileSync

Unimplemented (other modules): https, readline, tls, cluster, vm

Unimplemented (global/namespace): global/globalThis, __dirname, __filename,
                                  require/module/exports, fs.readFileSync(...) namespace calls

Unimplemented (async): timers setTimeout / setInterval / setImmediate / queueMicrotask,
                       real async I/O scheduling, keep-alive

Unimplemented (platform): Bun extension (design example only)
```
