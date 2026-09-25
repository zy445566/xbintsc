# xbintsc Node 扩展未实现功能

> 语言 / Language：[English](../node-unimplemented.md) | **简体中文**

本文档基于对 Node 扩展源码（`src/extensions/node/`）与 C 运行时（`runtime/ext_node/`）的逐文件核对整理，列出 **Node 扩展当前未实现 / 缺失** 的功能。

> 相关文档：
> - 核心语言能力见 [implemented.md](implemented.md) / [unimplemented.md](unimplemented.md)
> - Node 扩展已实现部分见 [node-implemented.md](node-implemented.md)

---

## 1. `fs` 模块未实现

`fs` 现在实现了**同步** API 表面（文件、目录、描述符、元数据、链接、`cp`、`glob`、`Dir`、`constants`），`fs/promises` 将每个同步函数包进已 settle 的 Promise（见 [node-implemented.md](node-implemented.md)）。其余仍未实现，因为 xbintsc **没有异步 I/O 调度器 / 事件循环**：

| 未实现 API | 类别 |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` / `open` / `close` / `read` / `write` / `stat` / … | 回调式异步文件操作（`fs/promises` 提供 Promise 形式） |
| `createReadStream` / `createWriteStream` | 流式读写 |
| `watch` / `watchFile` | **真正的**文件监听：函数存在但返回对象从不触发（事件循环无定时器/inotify 后端） |
| `openAsBlob`、`statfs` 回调形式、`rmdir` 的 `maxRetries` / `retryDelay` | 需异步重试的杂项选项 |
| `glob` 的 `exclude` / `follow`、`cp` 的 `filter` | 选项回调 |

### 1.1 读取语义缺口

- `readFileSync` 支持编码选项（默认/`utf8`/`ascii`/`latin1`/`binary`/`hex`/`base64`/`base64url`），但默认**不返回 `Buffer`**：即使提供了 `Buffer`（以普通对象模拟），`readFileSync` 仍以字符串返回。要读取原始字节请显式指定。
- 未实现 `utf16le` / `ucs2` 解码（按文本返回字节）。
- `watch` / `watchFile` / `unwatchFile` 返回 API 形态的发射器对象，`.close()` / `.on()` 方法存在但**不会触发**事件。
- `Dir.read(cb)` / `Dir.close(cb)` 与 `FileHandle` 异步方法都会**同步**完成（回调/Promise 立即被调用）。
- `mkdtempSync` 总是向前缀追加 6 个随机字符（不要求以 `XXXXXX` 结尾）。
- `globSync` 支持 `*`、`?`、`[...]`、`**`，但不支持 `exclude` 回调与 `follow`；`**` 不跟随符号链接（与 Node 默认一致）。
- `cpSync` 的符号链接处理在常见场景下与 Node 一致，但不支持 `verbatimSymlinks`。
- 平台差异：Windows 上 `readlinkSync` 抛 `ENOSYS`，`chmodSync` / `lchmodSync` / `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` 为空操作；`statfsSync` 在 Windows/AIX/Sun 返回全零字段；`lutimesSync` 在 macOS/Windows 退化为 `utimesSync`。

---

## 2. 其它 Node 模块完全未实现

以下 Node 内置模块没有任何对应扩展 / 内置函数（`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` / `dgram` / `http` / `events` / `util` / `querystring` / `assert` / `test` 已实现，`crypto` / `url` / `child_process` / `zlib` / `stream/promises` / `worker_threads` 部分实现，见 [node-implemented.md](node-implemented.md)）：

| 模块 | 说明 |
| --- | --- |
| `https` | TLS 版 HTTP |
| `readline` | 命令行读取 |
| `tls` / `cluster` / `vm` / `os`（部分）等 | 其余未列出的模块 |

> `crypto`（仅 `createHash`，现支持流式 API）、`url`（仅 `pathToFileURL` / `fileURLToPath`）、
> `child_process`（仅 `spawnSync`）、`zlib`（仅 `createGzip`）与
> `worker_threads`（仅 `Worker` / `isMainThread` / `workerData` / `parentPort`）目前为部分实现。

---

## 3. Node 全局对象 / 命名空间

| 未实现 | 说明 |
| --- | --- |
| `global` / `globalThis` | 无 |
| `__dirname` / `__filename` | 无 |
| `require` / `module` / `exports` | 无（xbintsc 无 CommonJS 模块运行时） |
| `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask` | 无定时器 |
| 未 `import` 的 `fs.readFileSync(...)` | ✗ 不支持。`fs` 不是全局对象；请先导入（`import fs from "fs"` 或 `import * as fs from "node:fs"`），之后 `fs.readFileSync(...)` 会下降为该模块的运行时符号。 |

Node 模块通过裸名称或 `node:` 前缀的 `import` 引入（`import { readFileSync } from "fs"`、`import path from "path"`、`import { platform } from "node:os"`）。具名与命名空间导入都会解析到扩展模块的运行时入口。

已支持的命名空间调用：`path.*`、`os.*`、`process.*`、`fs.*`、`fs/promises.*`（方法）、`child_process.*`、`crypto.*`、`url.*`（需先导入；`path`/`os`/`process` 也可使用全局名），以及基于分发器的 `Buffer.*`、`stream.*`、`net.*`、`dgram.*`、`http.*` 和 `process.platform` / `process.argv` 等属性。`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` / `Buffer` 亦可用作全局构造函数。

---

## 4. 异步 / 事件循环模型

已实现部分：

- 核心事件循环 `runtime/xt_loop.c`（`select(2)` 反应堆），`net` / `dgram` / `http` 均构建于其上。
- `Promise`、`async` / `await` 已可用（见核心文档），`fs/promises` 返回 Promise。
- 提供独立的 `EventEmitter`（`events` 模块，同时也是全局构造函数）；流与套接字额外具备内部发射器方法（`on` / `addListener` / `once` / `off` / `removeListener` / `emit`）。

仍未实现：

- 定时器 `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask`。
- 真正的异步 I/O 调度：`fs/promises` 实质是同步操作的即时 settle 包装，不会在等待 I/O 时让出。
- 流与套接字的内置 `once` 等同 `on`（不具「触发一次后移除」语义）；`events` 模块的 `EventEmitter` 实现了真正的 `once`。
- `net` 的 `connect` 为阻塞式；HTTP 响应要求 `Connection: close`，不做 keep-alive / 分块传输 / 流水线复用。

---

## 5. Bun 扩展未实现

设计文档中提到的 `Bun.file` 等 Bun API 仅作为扩展机制的设计示例，**尚未实现**：

- 无 `bun` 扩展目录、无 Bun 内置函数、无 Bun C 运行时。
- `src/extensions/` 下目前只有 `node` 一个平台扩展（外加核心扩展）。

---

## 6. 速查：Node 扩展未实现清单

```
已实现（fs）：完整同步表面 —— readFileSync（含编码）、readTextFile、
              writeFileSync、appendFileSync、existsSync、readdirSync（withFileTypes/recursive）、
              mkdirSync、rmSync、unlinkSync、rmdirSync、renameSync、copyFileSync、cpSync、
              realpathSync、statSync、lstatSync、statfsSync、accessSync、chmodSync、lchmodSync、
              chownSync、lchownSync、truncateSync、utimesSync、lutimesSync、mkdtempSync、
              linkSync、symlinkSync、readlinkSync、opendirSync（Dir）、globSync、watch、
              watchFile、unwatchFile、constants、
              openSync、closeSync、readSync、writeSync、readvSync、writevSync、fstatSync、
              fsyncSync、fdatasyncSync、ftruncateSync、fchmodSync、fchownSync、futimesSync
已实现（fs/promises）：readFile、writeFile、appendFile、mkdir、readdir、rm、unlink、
                      rmdir、rename、copyFile、cp、realpath、stat、lstat、statfs、access、
                      open（FileHandle）、chmod、lchmod、chown、lchown、truncate、utimes、
                      lutimes、link、symlink、readlink、mkdtemp、opendir、glob、watch、constants
已实现（其它模块）：path（join/resolve/normalize/dirname/basename/extname/isAbsolute/relative）、
                    os（platform/arch/type/release/endianness/homedir/tmpdir/hostname/totalmem/freemem/cpus）、
                    process（cwd/exit/uptime/hrtime/getuid/platform/arch/pid/ppid/argv/env/version/title）、
                    buffer（from/alloc/isBuffer/byteLength/concat/compare + 实例方法）、
                    stream（Readable/Writable/Duplex/Transform/PassThrough、pipe）、
                    net（createServer/connect/isIP + Server/Socket）、
                    dgram（createSocket + bind/send/close/address）、
                    http（createServer/request/get + req/res）、
                    events（EventEmitter：on/once/off/emit/listeners/listenerCount/eventNames）、
                    util（format/inspect/isDeepStrictEqual/inherits/promisify + isX）、
                    querystring（parse/stringify/escape/unescape）、
                    crypto（createHash + 流式 API，SHA-1/SHA-256）、
                    url（仅 pathToFileURL/fileURLToPath）、
                    child_process（仅 spawnSync）、
                    zlib（仅 createGzip）、stream/promises（仅 pipeline）、
                    worker_threads（仅 Worker/isMainThread/workerData/parentPort）

未实现（fs）：回调式异步文件操作（readFile/writeFile/appendFile/open/read/write/close）、
              createReadStream/createWriteStream、真正的文件监听（watch/watchFile 对象从不触发）、
              glob exclude/follow、cp filter、utf16le 解码、readFileSync 返回 Buffer

未实现（其它模块）：https、readline、tls、cluster、vm

未实现（全局/命名空间）：global/globalThis、__dirname、__filename、
                        require/module/exports、fs.readFileSync(...) 命名空间调用

未实现（异步）：定时器 setTimeout / setInterval / setImmediate / queueMicrotask、
                真正异步 I/O 调度、keep-alive

未实现（平台）：Bun 扩展（仅设计示例）
```
