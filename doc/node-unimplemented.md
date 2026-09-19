# xbintsc Node 扩展未实现功能

本文档基于对 Node 扩展源码（`src/extensions/node/`）与 C 运行时（`runtime/ext_node/`）的逐文件核对整理，列出 **Node 扩展当前未实现 / 缺失** 的功能。

> 相关文档：
> - 核心语言能力见 [implemented.md](implemented.md) / [unimplemented.md](unimplemented.md)
> - Node 扩展已实现部分见 [node-implemented.md](node-implemented.md)

---

## 1. `fs` 模块未实现

当前 `fs` 已实现同步读写、目录操作与 `statSync`（见 [node-implemented.md](node-implemented.md)），其余仍未实现：

| 未实现 API | 类别 |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` | 异步回调式读写 |
| `watch` / `watchFile` | 文件监听 |
| `openSync` / `closeSync` / `readSync` / `writeSync` | 文件描述符级操作 |
| `mkdtempSync` | 临时目录创建 |
| `linkSync` / `symlinkSync` / `readlinkSync` | 硬链接 / 符号链接 |
| `truncateSync` / `chmodSync` / `chownSync` / `utimesSync` | 元数据修改 |
| `createReadStream` / `createWriteStream` | 流 |
| `fs/promises` | Promise 版 API |

### 1.1 读取语义缺口

- `readFileSync` 支持编码选项（默认/`utf8`/`ascii`/`latin1`/`binary`/`hex`/`base64`），但**无法返回 `Buffer`/二进制**（xbintsc 目前没有 `Buffer` 值类型）。
- 错误处理与 Node 不符：打开失败只打印 stderr 并返回 `undefined`，不会抛出 `Error` / `ENOENT` 等异常（xbintsc 尚无可捕获异常体系）。

---

## 2. 其它 Node 模块完全未实现

以下 Node 内置模块没有任何对应扩展 / 内置函数（`fs` / `path` / `os` / `process` 已实现，见 [node-implemented.md](node-implemented.md)）：

| 模块 | 说明 |
| --- | --- |
| `crypto` | 哈希、随机数、加密 |
| `http` / `https` | HTTP 客户端 / 服务端 |
| `net` | TCP / IPC 网络 |
| `dgram` | UDP |
| `child_process` | `spawn` `exec` `fork` |
| `util` | `inspect` `format` `promisify` 等 |
| `stream` | 流 |
| `events` | `EventEmitter` |
| `buffer` | `Buffer` |
| `url` | URL 解析 |
| `querystring` | 查询字符串解析 |
| `zlib` | 压缩 / 解压 |
| `readline` | 命令行读取 |
| `worker_threads` | 工作线程 |
| `fs/promises` | Promise 版 fs API |

---

## 3. Node 全局对象 / 命名空间

| 未实现 | 说明 |
| --- | --- |
| `global` / `globalThis` | 无 |
| `__dirname` / `__filename` | 无 |
| `require` / `module` / `exports` | 无（xbintsc 无 CommonJS 模块运行时） |
| `Buffer` / `TypedArray` | 无二进制值类型 |
| `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask` | 无定时器 |
| 命名空间式调用 `fs.readFileSync(...)` | ✗ 不支持。`fs` 内置函数只能以裸全局标识符调用，如 `readFileSync(...)` |
| `import { readFileSync } from "fs"` | ✗ 无法运行。`import` 语句本身在代码生成阶段报 `UnsupportedFeature`（见 [unimplemented.md](unimplemented.md)）；扩展内置函数是全局符号映射，与 import 无关 |

已支持的命名空间调用：`path.*`、`os.*`、`process.*`（方法），以及 `process.platform` / `process.argv` 等属性。

---

## 4. 异步 / 事件循环模型未实现

- 无回调 / Promise 异步模型：`readFile` 等异步 API 无运行时支撑（无事件循环、无回调注册；核心 `Promise` 无法用于 I/O 回调）。
- 无 `async` / `await`（见核心 [unimplemented.md](unimplemented.md)）。
- 无定时器 `setTimeout` / `setInterval` / `setImmediate` / `queueMicrotask`。

---

## 5. Bun 扩展未实现

设计文档中提到的 `Bun.file` 等 Bun API 仅作为扩展机制的设计示例，**尚未实现**：

- 无 `bun` 扩展目录、无 Bun 内置函数、无 Bun C 运行时。
- `src/extensions/` 下目前只有 `node` 一个平台扩展（外加核心 `core` 扩展暴露 `print`）。

---

## 6. 速查：Node 扩展未实现清单

```
已实现（fs）：readFileSync（含编码）、readTextFile、writeFileSync、appendFileSync、
              existsSync、readdirSync、mkdirSync、rmSync、unlinkSync、rmdirSync、
              renameSync、copyFileSync、realpathSync、statSync、lstatSync
已实现（其它模块）：path（join/resolve/normalize/dirname/basename/extname/isAbsolute/relative）、
                    os（platform/arch/type/release/endianness/homedir/tmpdir/hostname/totalmem/freemem/cpus）、
                    process（cwd/exit/uptime/hrtime/getuid/platform/arch/pid/ppid/argv/env/version/title）

未实现（fs）：readFile、writeFile、appendFile、watch/watchFile、open/read/write/close、
              mkdtempSync、link/symlink/readlink、chmod/chown/utimes/truncate、
              createReadStream/createWriteStream、Buffer 返回

未实现（其它模块）：crypto、http/https、net、dgram、child_process、util、stream、
                    events、buffer、url、querystring、zlib、readline、
                    worker_threads、fs/promises

未实现（全局/命名空间）：global/globalThis、__dirname、__filename、
                        require/module/exports、Buffer、fs.readFileSync(...) 命名空间调用

未实现（异步）：回调 / Promise I/O / 事件循环 / setTimeout / async-await

未实现（平台）：Bun 扩展（仅设计示例）
```
