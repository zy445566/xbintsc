# xbintsc Node 扩展已实现功能

> 语言 / Language：[English](../node-implemented.md) | **简体中文**

本文档基于对 Node 扩展源码（`src/extensions/node/`）与 C 运行时（`runtime/ext_node/`）的逐文件核对整理，列出当前 **Node 扩展真正可用** 的功能与接口。

> 相关文档：
> - 核心语言能力见 [implemented.md](implemented.md) / [unimplemented.md](unimplemented.md)
> - Node 扩展未实现部分见 [node-unimplemented.md](node-unimplemented.md)

---

## 1. 扩展机制（已实现）

实现位置：`src/extensions/node/index.ts`、`src/extensions/node/module.ts`、`src/extensions/registry.ts`

- Node 扩展通过统一的 `Extension` 对象接入，仅在注册时编译并链接其 C 源。
- 通过 CLI 开启：

```bash
xbintsc run examples/node/read.ts --ext node
```

- 模块化组织：每个 Node 模块一个子目录，与 C 实现一一对应：

```
src/extensions/node/           runtime/ext_node/
  index.ts    # nodeExtension      fs/read_file.c
  module.ts   # NodeModule 接口    fs/write_file.c
  fs/index.ts                      fs/fs_ops.c
  fs/read-file.ts                  fs/fs_common.h
  fs/write-file.ts                 fs/promises.c
  fs/fs-ops.ts                     fs/streams.c
  fs/streams.ts                     fs/fd_ops.c
  path/index.ts                    fs/meta_ops.c
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
  assert/index.ts                   node_common.h（事件发射器 / 编码助手）
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

- 核心事件循环：`runtime/xt_loop.c`（`select(2)` 反应堆），生成模块的 `main` 在微任务清空后调用 `xt_run_event_loop()`；无可注册 fd 时立即返回，因此纯计算程序不受影响。

- `NodeModule` 接口：
  - `name`：模块名（如 `fs`）
  - `runtimeSources()`：该模块的 C 源
  - `builtins()`：全局标识符 → 运行时符号的映射（`path` / `os` / `process` 通过命名空间分发，故返回空表）
  - `namespace` / `exports()`：该模块的可导入命名空间与命名导出（`import { join } from "path"`、`import path from "path"`）
- `resolveFrom(importMetaUrl, relative)`：把相对于当前模块目录的路径解析为绝对路径，用于定位 C 源。

---

## 2. `fs` 模块（已实现，仅同步 API）

实现位置：`src/extensions/node/fs/*.ts`、`runtime/ext_node/fs/*.c`

### 2.1 可用函数（从 `fs` 导入）

| 导入函数 | 运行时符号 | 说明 |
| --- | --- | --- |
| `readFileSync(path[, options])` | `xt_node_read_text_file` | 同步读取文件，返回字符串；支持编码选项 |
| `readTextFile(path)` | `xt_node_read_text_file` | `readFileSync` 的别名（同一符号） |
| `writeFileSync(path, data[, options])` | `xt_node_write_file` | 覆盖写入；接受字符串或 `Buffer` |
| `appendFileSync(path, data[, options])` | `xt_node_append_file` | 追加写入；接受字符串或 `Buffer` |
| `existsSync(path)` | `xt_node_exists` | 是否存在，返回布尔值 |
| `readdirSync(path[, options])` | `xt_node_read_dir` | 目录项名称；`{ withFileTypes: true }` 返回 `Dirent`，`{ recursive: true }` 递归 |
| `mkdirSync(path[, options])` | `xt_node_mkdir` | 创建目录，`{ recursive: true }` 递归创建，`{ mode }` 生效 |
| `rmSync(path[, options])` | `xt_node_rm` | 删除文件/目录，`{ recursive: true }` 递归删除 |
| `unlinkSync(path)` | `xt_node_unlink` | 删除文件 |
| `rmdirSync(path)` | `xt_node_rmdir` | 删除空目录 |
| `renameSync(oldPath, newPath)` | `xt_node_rename` | 重命名 / 移动 |
| `copyFileSync(src, dest[, flags])` | `xt_node_copy_file` | 复制文件；支持 `COPYFILE_EXCL` |
| `cpSync(src, dest[, options])` | `xt_node_cp` | 递归复制；`recursive` / `force` / `errorOnExist` / `dereference` / `preserveTimestamps` |
| `realpathSync(path)` | `xt_node_realpath` | 解析为绝对路径 |
| `statSync(path)` | `xt_node_stat` | 文件元信息对象（跟随符号链接） |
| `lstatSync(path)` | `xt_node_lstat` | 文件元信息对象（不跟随符号链接） |
| `statfsSync(path)` | `xt_node_statfs` | 文件系统统计（`bsize` / `blocks` / `bfree` …） |
| `accessSync(path[, mode])` | `xt_node_access` | 检查可访问性 |
| `chmodSync(path, mode)` | `xt_node_chmod` | 修改权限 |
| `lchmodSync(path, mode)` | `xt_node_lchmod` | 修改符号链接权限 |
| `chownSync(path, uid, gid)` | `xt_node_chown` | 修改属主 |
| `lchownSync(path, uid, gid)` | `xt_node_lchown` | 修改符号链接属主 |
| `truncateSync(path[, len])` | `xt_node_truncate` | 截断文件 |
| `utimesSync(path, atime, mtime)` | `xt_node_utimes` | 设置访问/修改时间（秒数或 `Date`） |
| `lutimesSync(path, atime, mtime)` | `xt_node_lutimes` | 设置符号链接时间 |
| `mkdtempSync(prefix)` | `xt_node_mkdtemp` | 创建唯一临时目录 |
| `linkSync(existing, newPath)` | `xt_node_link` | 硬链接 |
| `symlinkSync(target, path[, type])` | `xt_node_symlink` | 符号链接 |
| `readlinkSync(path)` | `xt_node_readlink` | 读取符号链接目标 |
| `opendirSync(path[, options])` | `xt_node_opendir` | 返回 `Dir`（`readSync` / `closeSync` / `read` / `close`） |
| `globSync(pattern[, options])` | `xt_node_glob` | glob 匹配（`*` / `?` / `[...]` / `**`），`{ cwd, withFileTypes }` |
| `openSync(path[, flags[, mode]])` | `xt_node_open` | 打开文件描述符 |
| `closeSync(fd)` | `xt_node_close` | 关闭描述符 |
| `readSync(fd, buffer, offset, length, position)` | `xt_node_read` | 读入 `Buffer` |
| `writeSync(fd, data[, offset[, length[, position]]])` | `xt_node_write` | 写入字符串 / `Buffer` |
| `readvSync(fd, buffers[, position])` | `xt_node_readv` | 分散读 |
| `writevSync(fd, buffers[, position])` | `xt_node_writev` | 聚集写 |
| `fstatSync(fd)` | `xt_node_fstat` | 描述符的 `stat` |
| `fsyncSync(fd)` / `fdatasyncSync(fd)` | `xt_node_fsync` / `xt_node_fdatasync` | 刷新描述符 |
| `ftruncateSync(fd[, len])` | `xt_node_ftruncate` | 截断描述符 |
| `fchmodSync(fd, mode)` | `xt_node_fchmod` | 描述符的 `chmod` |
| `fchownSync(fd, uid, gid)` | `xt_node_fchown` | 描述符的 `chown` |
| `futimesSync(fd, atime, mtime)` | `xt_node_futimes` | 描述符的 `utimes` |
| `watch(filename[, options][, listener])` | `xt_node_watch` | 返回发射器形态的 watcher（不会触发，见 §2.6） |
| `watchFile(filename[, options], listener)` | `xt_node_watch_file` | 轮询形态的 `StatWatcher`（不会触发） |
| `unwatchFile(filename[, listener])` | `xt_node_unwatch_file` | 停止监听 |
| `constants` | `xt_fs_constants` | `F_OK` / `R_OK` / `W_OK` / `X_OK`、`COPYFILE_*`、`O_*`、`S_IF*`（宿主值） |
| `promises` | `xt_fs_promises` | `fs/promises` 门面（见 §11） |

### 2.2 编码支持

`readFileSync` / `writeFileSync` / `appendFileSync` 的 `options` 可以是编码字符串，也可以是 `{ encoding: "..." }`：

| 编码 | 读取 | 写入 |
| --- | --- | --- |
| 默认 / `utf8` / `utf-8` / `ascii` / `latin1` / `binary` | 原始 UTF-8 文本 | 按文本字节写入 |
| `hex` | 小写十六进制字符串 | 解析十六进制后写入 |
| `base64` | Base64 字符串 | 解析 Base64 后写入 |
| `base64url` | Base64url 字符串 | 解析 Base64url 后写入 |
| `utf16le` / `ucs2` | 按文本直接读取原始字节（不做 UTF-16 解码） | 按 UTF-8 文本写入 |

> `readFileSync` 默认仍返回**字符串**而不是 `Buffer`（即使 `Buffer` 类已存在），这样 `console.log(readFileSync(p))` 仍打印文本。

### 2.3 `statSync` 返回结构

返回普通对象，数值属性：`size`、`mode`、`uid`、`gid`、`dev`、`ino`、`nlink`、`rdev`、`blksize`、`blocks`、`atimeMs`、`mtimeMs`、`ctimeMs`、`birthtimeMs`。
方法（原生闭包，可调用）：`isFile()`、`isDirectory()`、`isSymbolicLink()`、`isFIFO()`、`isSocket()`、`isBlockDevice()`、`isCharacterDevice()`。

### 2.4 调用方式

`fs` 的导出通过 `import` 引入（也可用 `node:fs` 别名），不再以裸全局标识符暴露：

```ts
import { readFileSync, writeFileSync, existsSync } from "fs";

const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

### 2.5 错误处理

失败时函数会**抛出** Node 形态的 `Error` 对象，带有 `name`（`Error`）、`message`、`code`（如 `ENOENT`）、`errno`（数值 `errno`）、`syscall` 与 `path`。`existsSync` 仍返回布尔值，不会抛出。

```ts
import { readFileSync } from "fs";
try {
  readFileSync("/does/not/exist");
} catch (error) {
  console.log((error as any).code); // ENOENT
}
```

### 2.6 与 Node 的差异

- **没有异步 I/O 调度器**（事件循环是无定时器的 `select(2)` 反应堆），因此不提供回调式 `fs` 函数（`readFile`、`writeFile`、`open` 等）；请使用 `fs/promises` 或 `*Sync` 形式。
- `watch` / `watchFile` / `unwatchFile` 返回 API 形态的发射器对象，`.close()` / `.on()` 方法存在，但**不会触发**事件。
- `Dir.read(cb)` / `Dir.close(cb)` 会**同步**调用回调。
- `readFileSync` 默认返回字符串而不是 `Buffer`。
- `utf16le` / `ucs2` 读取时按原始字节处理（不做 UTF-16 解码）。
- `mkdtempSync` 无论前缀是否以 `XXXXXX` 结尾，都会追加 6 个随机字符。
- `globSync` 支持 `*`、`?`、`[...]`、`**`，但不支持 `exclude` 回调与 `follow`；`**` 不跟随符号链接（与 Node 默认一致）。
- `cpSync` 基于同步助手实现；除非 `dereference: true`，否则符号链接按符号链接复制。
- **Windows**：`readlinkSync` 抛 `ENOSYS`；`chmodSync` / `lchmodSync` / `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` 为空操作；`statfsSync` 返回全零字段。
- **macOS / Windows**：`lutimesSync` 退化为 `utimesSync`。

---

## 3. `path` 模块（已实现）

实现位置：`src/extensions/node/path/index.ts`、`runtime/ext_node/path/path.c`

采用 `path.<name>(...)` 命名空间调用，编译器将其降为 `xt_path_static(<name>, argc, argv)`。结果在所有平台上都以 POSIX `/` 分隔符输出（Windows 也接受 `/`），但输入可以使用 Windows 原生分隔符：在 Windows 上 `/` 与 `\` 都被识别，并保留盘符前缀（`C:`），因此自举后的编译器能正确解析带盘符的路径。以命名空间方式导入（`import path from "path"` / `import * as path from "path"`），或单独导入方法（`import { join } from "path"`）。

| 方法 | 说明 |
| --- | --- |
| `path.join(...parts)` | 拼接并规范化 |
| `path.resolve(...parts)` | 解析为绝对路径 |
| `path.normalize(path)` | 规范化 |
| `path.dirname(path)` | 目录名 |
| `path.basename(path[, ext])` | 文件名，可去掉扩展名 |
| `path.extname(path)` | 扩展名 |
| `path.isAbsolute(path)` | 是否绝对路径 |
| `path.relative(from, to)` | 相对路径 |

```ts
import path from "path";
import { basename } from "path";

console.log(path.join("a", "b", "..", "c")); // a/c
console.log(basename("/x/y/z.txt"));          // z.txt
```

---

## 4. `os` 模块（已实现）

实现位置：`src/extensions/node/os/index.ts`、`runtime/ext_node/os/os.c`

采用 `os.<name>(...)` 命名空间调用，降为 `xt_os_static(<name>, argc, argv)`。
以命名空间方式导入（`import os from "os"`），或单独导入函数（`import { platform } from "os"`）。

| 方法 | 说明 |
| --- | --- |
| `os.platform()` | `darwin` / `linux` / `win32` / ... |
| `os.arch()` | `x64` / `arm64` / `ia32` / `arm` |
| `os.type()` | `Darwin` / `Linux` / `Windows_NT` / ... |
| `os.release()` | 内核版本 |
| `os.endianness()` | `LE` / `BE` |
| `os.homedir()` | 用户主目录 |
| `os.tmpdir()` | 临时目录 |
| `os.hostname()` | 主机名 |
| `os.totalmem()` / `os.freemem()` | 总内存 / 可用内存（字节） |
| `os.cpus()` | CPU 条目数组（`model` / `speed` 占位） |

---

## 5. `process` 对象（已实现）

实现位置：`src/extensions/node/process/index.ts`、`runtime/ext_node/process/process.c`

方法调用降为 `xt_process_call(<name>, argc, argv)`，属性访问降为 `xt_process_get(<name>)`。
通过 `import process from "process"` 导入后使用。

| 方法 / 属性 | 说明 |
| --- | --- |
| `process.cwd()` | 当前工作目录 |
| `process.exit([code])` | 退出进程 |
| `process.uptime()` | 进程运行时间（秒） |
| `process.hrtime()` | `[秒, 纳秒]` 数组 |
| `process.getuid()` | 用户 ID（Windows 返回 0） |
| `process.platform` / `process.arch` | 平台 / 架构 |
| `process.pid` / `process.ppid` | 进程 ID / 父进程 ID |
| `process.argv` | 参数数组（`argv[0]` 为可执行文件） |
| `process.env` | 环境变量对象 |
| `process.version` / `process.title` | 占位字符串 |

> `argv` 由生成的 `main` 通过 `xt_set_program_args` 捕获后提供给运行时。

---

## 6. `buffer` 模块（已实现）

实现位置：`src/extensions/node/buffer/index.ts`、`runtime/ext_node/buffer/buffer.c`

xbintsc 没有原生的二进制值类型，`Buffer` 以**普通对象**表示：每个字节是数字属性 `"0".."n-1"`，再加一个 `length` 属性，并共享 `xt_buffer_proto()` 原型提供实例方法。`xt_node_is_buffer` / `xt_node_buffer_bytes` 供其它模块跨模块访问字节。

| 静态方法 | 说明 |
| --- | --- |
| `Buffer.from(value[, encoding])` | 从字符串（hex / base64 / utf8）、数组或 Buffer 构造 |
| `Buffer.alloc(size[, fill])` | 分配并填充 |
| `Buffer.allocUnsafe(size)` | 分配 |
| `Buffer.isBuffer(value)` | 判定 |
| `Buffer.byteLength(value[, encoding])` | 字节长度 |
| `Buffer.concat(list[, totalLength])` | 拼接 |
| `Buffer.compare(a, b)` | 比较 |

实例方法：`toString([encoding])`、`toJSON()`、`slice(start, end)`、`subarray(...)`、`equals(other)`、`compare(other)`、`copy(target[, targetStart, sourceStart, sourceEnd])`、`write(string[, offset[, length[, encoding]]])`、`fill(value)`、`reverse()`、`indexOf(value)`、`lastIndexOf(value)`、`includes(value)`、`keys()`、`values()`，以及 `readUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`、`writeUInt8/UInt16LE/UInt16BE/UInt32LE/UInt32BE`。

```ts
const buf = Buffer.from("hello");
console.log(buf.toString(), buf.length);      // hello 5
console.log(Buffer.alloc(4, 65).toString());  // AAAA
```

---

## 7. `stream` 模块（已实现）

实现位置：`src/extensions/node/stream/index.ts`、`runtime/ext_node/stream/stream.c`

`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` 作为**全局构造函数**使用（`new Readable()` 等）；`stream.Readable.from(...)` 等静态方法通过 `stream` 命名空间解析。流是 EventEmitter，采用**同步事件模型**：`on('data')` 时冲刷 `push` 缓冲，`write` 即时投递。

| 方法 | 说明 |
| --- | --- |
| `push(chunk)` / `read([n])` | Readable 端 |
| `write(chunk)` / `end([chunk])` | Writable 端 |
| `pipe(destination)` | 数据转发 |
| `on('data' / 'end' / 'finish')` | 事件 |
| `pause()` / `resume()` / `setEncoding(enc)` / `destroy()` | 流控制 |

---

## 8. `net` 模块（已实现）

实现位置：`src/extensions/node/net/index.ts`、`runtime/ext_node/net/net.c`

TCP 服务端与客户端，基于核心事件循环。

| API | 说明 |
| --- | --- |
| `net.createServer([connectionListener])` | 创建 TCP 服务端（`Server` 构造函数等价） |
| `net.connect(...)` / `net.createConnection(...)` | 连接（阻塞式 connect，之后注册事件循环） |
| `net.isIP(s)` / `net.isIPv4(s)` / `net.isIPv6(s)` | 地址判定 |

`Server`：`listen(port[, host][, cb])`、`close([cb])`、`address()`、`getConnections(cb)`，事件 `listening` / `connection` / `close`。

`Socket`：`write(data[, cb])`、`end([data])`、`destroy()`、`address()`、`setEncoding(enc)`、`pause()` / `resume()`，事件 `data` / `end` / `close` / `connect` / `error`。

---

## 9. `dgram` 模块（已实现）

实现位置：`src/extensions/node/dgram/index.ts`、`runtime/ext_node/dgram/dgram.c`

UDP 套接字。`dgram.createSocket(type | options[, cb])` 返回 EventEmitter。

| 方法 | 说明 |
| --- | --- |
| `bind([port][, address][, cb])` | 绑定（未绑定时 `send` 会自动绑定） |
| `send(msg[, offset, length,] port[, address][, cb])` | 发送数据报 |
| `close([cb])` / `address()` | 关闭 / 查询地址 |
| `setBroadcast(b)` / `setTTL(n)` / `setMulticastTTL(n)` | 套接字选项 |
| `on('message', (msg, rinfo) => ...)` | 收到数据报，`rinfo` 含 `address` / `port` / `family` / `size` |

---

## 10. `http` 模块（已实现）

实现位置：`src/extensions/node/http/index.ts`、`runtime/ext_node/http/http.c`

服务端包裹一个 `net` 服务端：每个连接累积字节直到完整请求（请求行 + 头 + `Content-Length` body）可用，再以 `req`/`res` 调用 `request` 监听器。客户端包裹一个 `net` 套接字，写出 HTTP/1.1 请求并在连接关闭后解析响应。

| API | 说明 |
| --- | --- |
| `http.createServer([requestListener])` | 创建 HTTP 服务端 |
| `http.request(options[, cb])` | 创建 `ClientRequest`（`write` / `end` / `setHeader`） |
| `http.get(url[, cb])` | 发起 GET |

`IncomingMessage`（`req` / 响应）：`method`、`url`、`httpVersion`、`headers`、`statusCode`、`data` / `end` 事件、`setEncoding`。

`ServerResponse`（`res`）：`writeHead(status[, message][, headers])`、`setHeader` / `getHeader` / `removeHeader` / `getHeaders`、`write(chunk)`、`end([chunk])`，事件 `finish` / `close`。响应固定带 `Connection: close`（不做 keep-alive）。

---

## 11. `fs/promises` 模块（已实现）

实现位置：`src/extensions/node/fs-promises/index.ts`、`runtime/ext_node/fs/promises.c`

无异步 I/O 调度器，故每个函数把对应的同步 `fs` 实现包进**已 settle 的 Promise**。失败时会以同步形式抛出的同构 Node `Error`（含 `code` / `errno` / `syscall` / `path`）**reject**。`fs.promises` 也可从 `fs` 模块访问（`import { promises as fsp } from "fs"`），两个命名空间都暴露 `constants` 对象。

Promise 导出：`readFile`、`writeFile`、`appendFile`、`mkdir`、`readdir`、`rm`、`unlink`、`rmdir`、`rename`、`copyFile`、`cp`、`realpath`、`stat`、`lstat`、`statfs`、`access`、`open`、`chmod`、`lchmod`、`chown`、`lchown`、`truncate`、`utimes`、`lutimes`、`link`、`symlink`、`readlink`、`mkdtemp`、`opendir`、`glob`、`watch`。

`open(...)` 解析为 **`FileHandle`**，带有：`read`、`write`、`readFile`、`writeFile`、`appendFile`、`close`、`stat`、`truncate`、`chmod`、`chown`、`utimes`、`sync`、`datasync`。

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

### 11.1 与 Node 的差异

- 由于 I/O 是同步的，Promise 在返回值被 `await` 之前就已 settle（事件循环不会让出）。
- `FileHandle.appendFile` 行为同 `writeFile`（在当前位置写入而非追加）。
- `FileHandle.readFile()` 从当前文件描述符偏移处读取。
- `Dir.read` / `Dir.close`（同步与 Promise/回调两种形式）都会立即完成。
- `watch` 解析为与 `fs.watch` 相同的不会触发的 watcher 对象。

---

## 12. `child_process` 模块（已实现）

位置：`src/extensions/node/child_process/index.ts`、`runtime/ext_node/child_process/child_process.c`

`spawnSync(command, args[, options])` 运行一个程序直到结束，返回
`{ status, stdout, stderr }`。`options.cwd` 设置子进程工作目录；
`options.stdio: "inherit"` 让子进程共用父进程的 stdout/stderr（`xbintsc run`
用它把被编译程序的输出实时透传），否则 stdout/stderr 会作为 UTF-8 字符串捕获。

| 选项 | 说明 |
| --- | --- |
| `cwd` | 子进程的工作目录 |
| `stdio: "inherit"` | 共用父进程的 stdout/stderr，而不是捕获 |
| `encoding` | 接受但忽略（输出始终按 UTF-8 解码） |

```ts
import { spawnSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);
```

---

## 13. `events` 模块（已实现）

位置：`src/extensions/node/events/index.ts`、`runtime/ext_node/events/events.c`

提供独立的 `EventEmitter`，既可以作为**全局构造函数**使用
（`new EventEmitter()`），也可以作为命名导出（`import { EventEmitter } from
"events"`）。实例与 `stream` / `net` / `http` 共用运行时事件发射器（监听器存放
在内部的 `__xt_events` 属性上），并在此基础上提供更完整的 `events` 接口：

| 方法 | 说明 |
| --- | --- |
| `on(name, fn)` / `addListener(name, fn)` | 追加监听器 |
| `once(name, fn)` | 最多触发一次，然后自行移除 |
| `prependListener(name, fn)` / `prependOnceListener(name, fn)` | 插入到最前 |
| `off(name, fn)` / `removeListener(name, fn)` | 移除监听器 |
| `removeAllListeners([name])` | 清空某个（或全部）事件 |
| `emit(name[, ...args])` | 触发监听器 |
| `listeners(name)` / `rawListeners(name)` | 监听器数组 |
| `listenerCount(name)` | 监听器数量 |
| `eventNames()` | 当前有监听器的事件名 |
| `setMaxListeners(n)` / `getMaxListeners()` | 记录（默认 10） |

通过命名空间（`import ee from "events"`）可访问的静态方法：`listenerCount`、
`getEventListeners`、`getMaxListeners`、`setMaxListeners`、`once`、
`addAbortListener`。

```ts
import { EventEmitter } from "events";

const em = new EventEmitter();
em.once("ready", () => console.log("ready"));
em.emit("ready"); // ready
em.emit("ready"); // 无输出：监听器已执行过
```

---

## 14. `util` 模块（已实现）

位置：`src/extensions/node/util/index.ts`、`runtime/ext_node/util/util.c`

同时支持命名导入（`import { format } from "util"`）与命名空间调用
（`import util from "util"` / `import * as util from "util"`）。

| 函数 | 说明 |
| --- | --- |
| `format(fmt, ...args)` | 支持 `%s` `%d` `%i` `%f` `%j` `%o` `%O` `%c` `%%` 占位符 |
| `formatWithOptions(opts, fmt, ...args)` | 接受选项但忽略 |
| `inspect(value)` | 递归打印（限制深度，字符串带引号） |
| `isDeepStrictEqual(a, b)` | 结构比较（`NaN` 等于 `NaN`） |
| `inherits(ctor, superCtor)` | 连接原型链 |
| `deprecate(fn, msg)` | 原样返回 `fn`（没有告警通道） |
| `promisify(fn)` | 把「回调在末尾」的函数包装为 `Promise` |
| `isString` `isNumber` `isBoolean` `isUndefined` `isNull` `isFunction` `isArray` `isObject` `isBuffer` `isDate` `isRegExp` `isPromise` `isError` | 类型判断 |

```ts
import { format, promisify } from "util";

console.log(format("%s=%d", "n", 3)); // n=3
```

---

## 15. `querystring` 模块（已实现）

位置：`src/extensions/node/querystring/index.ts`、`runtime/ext_node/querystring/querystring.c`

| 函数 | 说明 |
| --- | --- |
| `parse(str[, sep[, eq]])` / `decode` | 解析为对象；重复的键会变成数组 |
| `stringify(obj[, sep[, eq]])` / `encode` | 序列化；空格变成 `+`，数组会重复键 |
| `escape(str)` / `unescape(str)` | 百分号编码 / 解码（`+` 解码为空格） |

默认值：`sep = "&"`，`eq = "="`。

```ts
import { parse, stringify } from "querystring";

const q = parse("a=1&b=2&b=3");      // { a: "1", b: ["2", "3"] }
console.log(stringify({ x: "a b" })); // x=a+b
```

---

## 16. 已实现 Node 能力速查

| 类别 | 内容 |
| --- | --- |
| 扩展注册 | `nodeExtension`（`--ext node`）、`NodeModule` 接口、`resolveFrom` 工具 |
| fs 读取 | `readFileSync`、`readTextFile`（`xt_node_read_text_file`），支持 hex / base64 / base64url |
| fs 写入 | `writeFileSync`、`appendFileSync`，支持 hex / base64 / base64url；字符串或 `Buffer` |
| fs 目录 | `readdirSync`（`withFileTypes` / `recursive`）、`mkdirSync`、`rmSync`、`unlinkSync`、`rmdirSync`、`opendirSync`（`Dir`）、`globSync`、`mkdtempSync` |
| fs 其它 | `existsSync`、`renameSync`、`copyFileSync`、`cpSync`、`realpathSync`、`statSync`、`lstatSync`、`statfsSync`、`accessSync`、`chmodSync`、`chownSync`、`lchmodSync`、`lchownSync`、`truncateSync`、`utimesSync`、`lutimesSync`、`linkSync`、`symlinkSync`、`readlinkSync`、`watch`、`watchFile`、`unwatchFile` |
| fs 描述符 | `openSync`、`closeSync`、`readSync`、`writeSync`、`readvSync`、`writevSync`、`fstatSync`、`fsyncSync`、`fdatasyncSync`、`ftruncateSync`、`fchmodSync`、`fchownSync`、`futimesSync` |
| fs 常量 | `constants`（`F_OK`、`R_OK`、`W_OK`、`X_OK`、`COPYFILE_*`、`O_*`、`S_IF*`） |
| path | `join` `resolve` `normalize` `dirname` `basename` `extname` `isAbsolute` `relative` |
| os | `platform` `arch` `type` `release` `endianness` `homedir` `tmpdir` `hostname` `totalmem` `freemem` `cpus` |
| process | `cwd` `exit` `uptime` `hrtime` `getuid`；`platform` `arch` `pid` `ppid` `argv` `env` `version` `title` |
| buffer | `Buffer.from/alloc/allocUnsafe/isBuffer/byteLength/concat/compare`；实例 `toString/toJSON/slice/.../readUInt32BE/writeUInt32BE` |
| stream | `Readable` `Writable` `Duplex` `Transform` `PassThrough`；`push/read/write/end/pipe/on` |
| net | `createServer` `connect` `createConnection` `isIP/isIPv4/isIPv6`；`Server` `Socket` |
| dgram | `createSocket`；`bind/send/close/address/setBroadcast/setTTL` |
| http | `createServer` `request` `get`；`ClientRequest`、`IncomingMessage`、`ServerResponse` |
| child_process | `spawnSync(command, args[, {cwd, stdio}])`，返回 `status` / `stdout` / `stderr` |
| assert | `ok/equal/notEqual/strictEqual/notStrictEqual/deepStrictEqual/notDeepStrictEqual/throws/doesNotThrow/ifError/match/doesNotMatch/fail`（`import assert from "node:assert"`） |
| test | `test(name, fn)` / `it` / `describe` / `skip` / `todo`（`import test from "node:test"`），输出 TAP，失败时以非零码退出 |
| zlib | `createGzip()`（由 `stream/promises` 的 pipeline 消费） |
| stream/promises | `pipeline(...)`（同步执行，返回已决议的 Promise） |
| worker_threads | `Worker`、`isMainThread`、`workerData`、`parentPort`（把当前可执行文件作为子进程重跑） |
| events | `EventEmitter`（全局 + 命名）；`on/once/off/emit/listeners/listenerCount/eventNames`；静态 `listenerCount/getEventListeners/getMaxListeners/setMaxListeners/once/addAbortListener` |
| util | `format` `formatWithOptions` `inspect` `isDeepStrictEqual` `inherits` `deprecate` `promisify`；`isString/isNumber/isBoolean/isUndefined/isNull/isFunction/isArray/isObject/isBuffer/isDate/isRegExp/isPromise/isError` |
| querystring | `parse`/`decode` `stringify`/`encode` `escape` `unescape` |
| crypto | `createHash(algorithm)`，含 `update`/`digest` 与流式 API（`setEncoding`/`write`/`end`/`read`）；支持 SHA-1 与 SHA-256 |
| 全局函数 | `btoa` / `atob` base64 辅助函数 |
| url | `pathToFileURL` `fileURLToPath` |
| fs/promises | `readFile` `writeFile` `appendFile` `mkdir` `readdir` `rm` `unlink` `rmdir` `rename` `copyFile` `cp` `realpath` `stat` `lstat` `statfs` `access` `open` `chmod` `lchmod` `chown` `lchown` `truncate` `utimes` `lutimes` `link` `symlink` `readlink` `mkdtemp` `opendir` `glob` `watch` `constants`；`FileHandle` |
| 事件循环 | `xt_loop`（`select` 反应堆）、`xt_run_event_loop()`、`xt_loop_add/update/remove` |
| 调用约定 | 统一 `(argc, argv)` ABI，返回 `xt_value` |
| 链接方式 | 注册后编译 `runtime/ext_node/**` 并随运行时一起链接 |
