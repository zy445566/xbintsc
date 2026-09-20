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
xbintsc run examples/read-file.ts --ext node
```

- 模块化组织：每个 Node 模块一个子目录，与 C 实现一一对应：

```
src/extensions/node/           runtime/ext_node/
  index.ts    # nodeExtension      fs/read_file.c
  module.ts   # NodeModule 接口    fs/write_file.c
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
  http/index.ts                    node_common.h（事件发射器 / 编码助手）
  fs-promises/index.ts
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
| `writeFileSync(path, data[, options])` | `xt_node_write_file` | 覆盖写入 |
| `appendFileSync(path, data[, options])` | `xt_node_append_file` | 追加写入 |
| `existsSync(path)` | `xt_node_exists` | 是否存在，返回布尔值 |
| `readdirSync(path)` | `xt_node_read_dir` | 返回目录项名称数组 |
| `mkdirSync(path[, options])` | `xt_node_mkdir` | 创建目录，`{ recursive: true }` 递归创建 |
| `rmSync(path[, options])` | `xt_node_rm` | 删除文件/目录，`{ recursive: true }` 递归删除 |
| `unlinkSync(path)` | `xt_node_unlink` | 删除文件 |
| `rmdirSync(path)` | `xt_node_rmdir` | 删除空目录 |
| `renameSync(oldPath, newPath)` | `xt_node_rename` | 重命名 / 移动 |
| `copyFileSync(src, dest)` | `xt_node_copy_file` | 复制文件 |
| `realpathSync(path)` | `xt_node_realpath` | 解析为绝对路径 |
| `statSync(path)` | `xt_node_stat` | 文件元信息对象（跟随符号链接） |
| `lstatSync(path)` | `xt_node_lstat` | 文件元信息对象（不跟随符号链接） |

### 2.2 编码支持

`readFileSync` / `writeFileSync` / `appendFileSync` 的 `options` 可以是编码字符串，也可以是 `{ encoding: "..." }`：

| 编码 | 读取 | 写入 |
| --- | --- | --- |
| 默认 / `utf8` / `utf-8` / `ascii` / `latin1` / `binary` | 原始 UTF-8 文本 | 按文本字节写入 |
| `hex` | 小写十六进制字符串 | 解析十六进制后写入 |
| `base64` | Base64 字符串 | 解析 Base64 后写入 |

> 由于 xbintsc 目前没有 `Buffer` 值类型，二进制读取始终以字符串返回。

### 2.3 `statSync` 返回结构

返回普通对象，数值属性：`size`、`mode`、`uid`、`gid`、`dev`、`ino`、`nlink`、`rdev`、`blksize`、`blocks`、`mtimeMs`、`atimeMs`、`ctimeMs`。
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

打开 / 操作失败时向 stderr 打印 `xbintsc: cannot ... 'path'` 并返回 `undefined`（`existsSync` 返回 `false`），不会抛出 `Error` / `ENOENT` 异常（xbintsc 尚无可捕获异常体系）。

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

无异步 I/O 调度器，故每个函数把对应的同步 `fs` 实现包进**已 settle 的 Promise**，由 `fs/promises` 模块导出：`readFile`、`writeFile`、`appendFile`、`mkdir`、`readdir`、`rm`、`unlink`、`rmdir`、`rename`、`copyFile`、`realpath`、`stat`、`lstat`、`access`。

```ts
import { readFile, writeFile } from "fs/promises";

async function main(): Promise<void> {
  await writeFile("/tmp/a.txt", "hi");
  console.log(await readFile("/tmp/a.txt"));
}
main();
```

---

## 12. 已实现 Node 能力速查

| 类别 | 内容 |
| --- | --- |
| 扩展注册 | `nodeExtension`（`--ext node`）、`NodeModule` 接口、`resolveFrom` 工具 |
| fs 读取 | `readFileSync`、`readTextFile`（`xt_node_read_text_file`），支持 hex / base64 |
| fs 写入 | `writeFileSync`、`appendFileSync`，支持 hex / base64 |
| fs 目录 | `readdirSync`、`mkdirSync`、`rmSync`、`unlinkSync`、`rmdirSync` |
| fs 其它 | `existsSync`、`renameSync`、`copyFileSync`、`realpathSync`、`statSync`、`lstatSync` |
| path | `join` `resolve` `normalize` `dirname` `basename` `extname` `isAbsolute` `relative` |
| os | `platform` `arch` `type` `release` `endianness` `homedir` `tmpdir` `hostname` `totalmem` `freemem` `cpus` |
| process | `cwd` `exit` `uptime` `hrtime` `getuid`；`platform` `arch` `pid` `ppid` `argv` `env` `version` `title` |
| buffer | `Buffer.from/alloc/allocUnsafe/isBuffer/byteLength/concat/compare`；实例 `toString/toJSON/slice/.../readUInt32BE/writeUInt32BE` |
| stream | `Readable` `Writable` `Duplex` `Transform` `PassThrough`；`push/read/write/end/pipe/on` |
| net | `createServer` `connect` `createConnection` `isIP/isIPv4/isIPv6`；`Server` `Socket` |
| dgram | `createSocket`；`bind/send/close/address/setBroadcast/setTTL` |
| http | `createServer` `request` `get`；`ClientRequest`、`IncomingMessage`、`ServerResponse` |
| fs/promises | `readFile` `writeFile` `appendFile` `mkdir` `readdir` `rm` `unlink` `rmdir` `rename` `copyFile` `realpath` `stat` `lstat` `access` |
| 事件循环 | `xt_loop`（`select` 反应堆）、`xt_run_event_loop()`、`xt_loop_add/update/remove` |
| 调用约定 | 统一 `(argc, argv)` ABI，返回 `xt_value` |
| 链接方式 | 注册后编译 `runtime/ext_node/**` 并随运行时一起链接 |
