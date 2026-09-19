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
xbintsc run examples/read-file.ts --ext node
```

- Modular organisation: one subdirectory per Node module, in one-to-one correspondence with its C implementation:

```
src/extensions/node/           runtime/ext_node/
  index.ts    # nodeExtension      fs/read_file.c
  module.ts   # NodeModule iface   fs/write_file.c
  fs/index.ts                      fs/fs_ops.c
  fs/read-file.ts                  fs/fs_common.h
  fs/write-file.ts                 fs/promises.c
  fs/fs-ops.ts                     path/path.c
  path/index.ts                    os/os.c
  os/index.ts                      process/process.c
  process/index.ts                 buffer/buffer.c
  buffer/index.ts                  stream/stream.c
  stream/index.ts                  net/net.c
  net/index.ts                     dgram/dgram.c
  dgram/index.ts                   http/http.c
  http/index.ts                    node_common.h (event emitter / encoding helpers)
  fs-promises/index.ts
```

- Core event loop: `runtime/xt_loop.c` (a `select(2)` reactor). The generated module's `main` calls `xt_run_event_loop()` after draining microtasks; it returns immediately when no fds are registered, so pure-computation programs are unaffected.

- The `NodeModule` interface:
  - `name`: the module name (e.g. `fs`)
  - `runtimeSources()`: the module's C sources
  - `builtins()`: global identifier → runtime symbol mapping (`path` / `os` / `process` dispatch through namespaces, so they return an empty table)
- `resolveFrom(importMetaUrl, relative)`: resolves a path relative to the current module's directory into an absolute path, used to locate C sources.

---

## 2. The `fs` module (implemented, synchronous API only)

Location: `src/extensions/node/fs/*.ts`, `runtime/ext_node/fs/*.c`

### 2.1 Available functions (bare global identifiers)

| Global function | Runtime symbol | Notes |
| --- | --- | --- |
| `readFileSync(path[, options])` | `xt_node_read_text_file` | synchronously read a file, returns a string; supports encoding options |
| `readTextFile(path)` | `xt_node_read_text_file` | alias of `readFileSync` (same symbol) |
| `writeFileSync(path, data[, options])` | `xt_node_write_file` | overwrite write |
| `appendFileSync(path, data[, options])` | `xt_node_append_file` | append write |
| `existsSync(path)` | `xt_node_exists` | whether it exists, returns a boolean |
| `readdirSync(path)` | `xt_node_read_dir` | returns an array of directory entry names |
| `mkdirSync(path[, options])` | `xt_node_mkdir` | create a directory; `{ recursive: true }` creates recursively |
| `rmSync(path[, options])` | `xt_node_rm` | delete a file/directory; `{ recursive: true }` deletes recursively |
| `unlinkSync(path)` | `xt_node_unlink` | delete a file |
| `rmdirSync(path)` | `xt_node_rmdir` | delete an empty directory |
| `renameSync(oldPath, newPath)` | `xt_node_rename` | rename / move |
| `copyFileSync(src, dest)` | `xt_node_copy_file` | copy a file |
| `realpathSync(path)` | `xt_node_realpath` | resolve to an absolute path |
| `statSync(path)` | `xt_node_stat` | file metadata object (follows symlinks) |
| `lstatSync(path)` | `xt_node_lstat` | file metadata object (does not follow symlinks) |

### 2.2 Encoding support

The `options` of `readFileSync` / `writeFileSync` / `appendFileSync` may be an
encoding string or `{ encoding: "..." }`:

| Encoding | Read | Write |
| --- | --- | --- |
| default / `utf8` / `utf-8` / `ascii` / `latin1` / `binary` | raw UTF-8 text | write as text bytes |
| `hex` | lowercase hex string | parse hex then write |
| `base64` | Base64 string | parse Base64 then write |

> Since xbintsc currently has no `Buffer` value type, binary reads are always returned as strings.

### 2.3 `statSync` return structure

Returns a plain object with numeric properties: `size`, `mode`, `uid`, `gid`, `dev`, `ino`, `nlink`, `rdev`, `blksize`, `blocks`, `mtimeMs`, `atimeMs`, `ctimeMs`.
Methods (native closures, callable): `isFile()`, `isDirectory()`, `isSymbolicLink()`, `isFIFO()`, `isSocket()`, `isBlockDevice()`, `isCharacterDevice()`.

### 2.4 How to call

`fs` builtins are exposed as **bare global identifiers** (not `fs.readFileSync(...)`):

```ts
const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

### 2.5 Error handling

On open / operation failure, it prints `xbintsc: cannot ... 'path'` to stderr and
returns `undefined` (`existsSync` returns `false`); it does not throw `Error` /
`ENOENT` exceptions (xbintsc does not yet have a catchable exception system).

---

## 3. The `path` module (implemented)

Location: `src/extensions/node/path/index.ts`, `runtime/ext_node/path/path.c`

Uses `path.<name>(...)` namespace calls, which the compiler lowers to
`xt_path_static(<name>, argc, argv)`. The semantics are POSIX (`/` separator).

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
console.log(path.join("a", "b", "..", "c")); // a/c
console.log(path.basename("/x/y/z.txt"));    // z.txt
```

---

## 4. The `os` module (implemented)

Location: `src/extensions/node/os/index.ts`, `runtime/ext_node/os/os.c`

Uses `os.<name>(...)` namespace calls, lowered to `xt_os_static(<name>, argc, argv)`.

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
synchronous `fs` implementation in an **already-settled Promise**, exposed as
bare global identifiers: `readFile`, `writeFile`, `appendFile`, `mkdir`,
`readdir`, `rm`, `unlink`, `rmdir`, `rename`, `copyFile`, `realpath`, `stat`,
`lstat`, `access`.

```ts
async function main(): Promise<void> {
  await writeFile("/tmp/a.txt", "hi");
  console.log(await readFile("/tmp/a.txt"));
}
main();
```

---

## 12. Implemented Node capabilities quick reference

| Category | Contents |
| --- | --- |
| Extension registration | `nodeExtension` (`--ext node`), `NodeModule` interface, `resolveFrom` utility |
| fs read | `readFileSync`, `readTextFile` (`xt_node_read_text_file`), supports hex / base64 |
| fs write | `writeFileSync`, `appendFileSync`, supports hex / base64 |
| fs directories | `readdirSync`, `mkdirSync`, `rmSync`, `unlinkSync`, `rmdirSync` |
| fs other | `existsSync`, `renameSync`, `copyFileSync`, `realpathSync`, `statSync`, `lstatSync` |
| path | `join` `resolve` `normalize` `dirname` `basename` `extname` `isAbsolute` `relative` |
| os | `platform` `arch` `type` `release` `endianness` `homedir` `tmpdir` `hostname` `totalmem` `freemem` `cpus` |
| process | `cwd` `exit` `uptime` `hrtime` `getuid`; `platform` `arch` `pid` `ppid` `argv` `env` `version` `title` |
| buffer | `Buffer.from/alloc/allocUnsafe/isBuffer/byteLength/concat/compare`; instances `toString/toJSON/slice/.../readUInt32BE/writeUInt32BE` |
| stream | `Readable` `Writable` `Duplex` `Transform` `PassThrough`; `push/read/write/end/pipe/on` |
| net | `createServer` `connect` `createConnection` `isIP/isIPv4/isIPv6`; `Server` `Socket` |
| dgram | `createSocket`; `bind/send/close/address/setBroadcast/setTTL` |
| http | `createServer` `request` `get`; `ClientRequest`, `IncomingMessage`, `ServerResponse` |
| fs/promises | `readFile` `writeFile` `appendFile` `mkdir` `readdir` `rm` `unlink` `rmdir` `rename` `copyFile` `realpath` `stat` `lstat` `access` |
| Event loop | `xt_loop` (`select` reactor), `xt_run_event_loop()`, `xt_loop_add/update/remove` |
| Calling convention | uniform `(argc, argv)` ABI, returns `xt_value` |
| Linking | after registration, compiles `runtime/ext_node/**` and links it with the runtime |
