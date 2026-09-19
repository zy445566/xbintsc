# xbintsc Node 扩展未实现功能

> 语言 / Language：[English](../node-unimplemented.md) | **简体中文**

本文档基于对 Node 扩展源码（`src/extensions/node/`）与 C 运行时（`runtime/ext_node/`）的逐文件核对整理，列出 **Node 扩展当前未实现 / 缺失** 的功能。

> 相关文档：
> - 核心语言能力见 [implemented.md](implemented.md) / [unimplemented.md](unimplemented.md)
> - Node 扩展已实现部分见 [node-implemented.md](node-implemented.md)

---

## 1. `fs` 模块未实现

当前 `fs` 已实现同步读写、目录操作与 `statSync`，`fs/promises` 已以「包装同步实现」的形式提供（见 [node-implemented.md](node-implemented.md)），其余仍未实现：

| 未实现 API | 类别 |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` | 异步回调式读写（`fs/promises` 提供了 Promise 版） |
| `watch` / `watchFile` | 文件监听 |
| `openSync` / `closeSync` / `readSync` / `writeSync` | 文件描述符级操作 |
| `mkdtempSync` | 临时目录创建 |
| `linkSync` / `symlinkSync` / `readlinkSync` | 硬链接 / 符号链接 |
| `truncateSync` / `chmodSync` / `chownSync` / `utimesSync` | 元数据修改 |
| `createReadStream` / `createWriteStream` | 流式读写 |

### 1.1 读取语义缺口

- `readFileSync` 支持编码选项（默认/`utf8`/`ascii`/`latin1`/`binary`/`hex`/`base64`），但**不返回 `Buffer`**：即使提供了 `Buffer`（以普通对象模拟），`readFileSync` 仍以字符串返回。
- `fs/promises` 的错误处理与 Node 不符：底层同步实现失败只打印 stderr 并解析为 `undefined`，**Promise 不会 reject**（xbintsc 尚无可捕获异常体系）。
- 错误处理与 Node 不符：打开失败只打印 stderr 并返回 `undefined`，不会抛出 `Error` / `ENOENT` 等异常（xbintsc 尚无可捕获异常体系）。

---

## 2. 其它 Node 模块完全未实现

以下 Node 内置模块没有任何对应扩展 / 内置函数（`fs` / `fs/promises` / `path` / `os` / `process` / `buffer` / `stream` / `net` / `dgram` / `http` 已实现，见 [node-implemented.md](node-implemented.md)）：

| 模块 | 说明 |
| --- | --- |
| `crypto` | 哈希、随机数、加密 |
| `https` | TLS 版 HTTP |
| `child_process` | `spawn` `exec` `fork` |
| `util` | `inspect` `format` `promisify` 等 |
| `events` | 独立的 `EventEmitter` 类（流/套接字内部已具备发射器能力，但无 `events` 模块 / 全局 `EventEmitter`） |
| `url` | URL 解析 |
| `querystring` | 查询字符串解析 |
| `zlib` | 压缩 / 解压 |
| `readline` | 命令行读取 |
| `worker_threads` | 工作线程 |
| `tls` / `cluster` / `vm` / `os`（部分）等 | 其余未列出的模块 |

---

## 3. Node 全局对象 / 命名空间

| 未实现 | 说明 |
| --- | --- |
| `global` / `globalThis` | 无 |
| `__dirname` / `__filename` | 无 |
| `require` / `module` / `exports` | 无（xbintsc 无 CommonJS 模块运行时） |
| `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask` | 无定时器 |
| `fs.readFileSync(...)` 命名空间式调用 | ✗ 不支持。`fs` 内置函数只能以裸全局标识符调用，如 `readFileSync(...)`；`fs/promises` 同理 |
| `import { readFileSync } from "fs"` | ✗ 无法运行。`import` 语句本身在代码生成阶段报 `UnsupportedFeature`（见 [unimplemented.md](unimplemented.md)）；扩展内置函数是全局符号映射，与 import 无关 |

已支持的命名空间调用：`path.*`、`os.*`、`process.*`（方法）、`Buffer.*`、`stream.*`、`net.*`、`dgram.*`、`http.*`，以及 `process.platform` / `process.argv` 等属性。`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` / `Buffer` 亦可用作全局构造函数。

---

## 4. 异步 / 事件循环模型

已实现部分：

- 核心事件循环 `runtime/xt_loop.c`（`select(2)` 反应堆），`net` / `dgram` / `http` 均构建于其上。
- `Promise`、`async` / `await` 已可用（见核心文档），`fs/promises` 返回 Promise。
- 事件发射器（`on` / `addListener` / `once` / `off` / `removeListener` / `emit`）已在流与套接字上提供。

仍未实现：

- 定时器 `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask`。
- 真正的异步 I/O 调度：`fs/promises` 实质是同步操作的即时 settle 包装，不会在等待 I/O 时让出。
- `once` 目前等同 `on`（不具「触发一次后移除」语义）。
- `net` 的 `connect` 为阻塞式；HTTP 响应要求 `Connection: close`，不做 keep-alive / 分块传输 / 流水线复用。

---

## 5. Bun 扩展未实现

设计文档中提到的 `Bun.file` 等 Bun API 仅作为扩展机制的设计示例，**尚未实现**：

- 无 `bun` 扩展目录、无 Bun 内置函数、无 Bun C 运行时。
- `src/extensions/` 下目前只有 `node` 一个平台扩展（外加核心扩展）。

---

## 6. 速查：Node 扩展未实现清单

```
已实现（fs）：readFileSync（含编码）、readTextFile、writeFileSync、appendFileSync、
              existsSync、readdirSync、mkdirSync、rmSync、unlinkSync、rmdirSync、
              renameSync、copyFileSync、realpathSync、statSync、lstatSync
已实现（fs/promises）：readFile、writeFile、appendFile、mkdir、readdir、rm、unlink、
                      rmdir、rename、copyFile、realpath、stat、lstat、access
已实现（其它模块）：path（join/resolve/normalize/dirname/basename/extname/isAbsolute/relative）、
                    os（platform/arch/type/release/endianness/homedir/tmpdir/hostname/totalmem/freemem/cpus）、
                    process（cwd/exit/uptime/hrtime/getuid/platform/arch/pid/ppid/argv/env/version/title）、
                    buffer（from/alloc/isBuffer/byteLength/concat/compare + 实例方法）、
                    stream（Readable/Writable/Duplex/Transform/PassThrough、pipe）、
                    net（createServer/connect/isIP + Server/Socket）、
                    dgram（createSocket + bind/send/close/address）、
                    http（createServer/request/get + req/res）

未实现（fs）：readFile、writeFile、appendFile（异步回调式）、watch/watchFile、
              open/read/write/close、mkdtempSync、link/symlink/readlink、
              chmod/chown/utimes/truncate、createReadStream/createWriteStream、Buffer 返回

未实现（其它模块）：crypto、https、child_process、util、events（独立类）、url、
                    querystring、zlib、readline、worker_threads、tls、cluster、vm

未实现（全局/命名空间）：global/globalThis、__dirname、__filename、
                        require/module/exports、fs.readFileSync(...) 命名空间调用

未实现（异步）：定时器 setTimeout / setInterval / setImmediate / queueMicrotask、
                真正异步 I/O 调度、once「触发一次」语义、keep-alive

未实现（平台）：Bun 扩展（仅设计示例）
```
