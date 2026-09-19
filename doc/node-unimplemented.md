# xbintsc Node 扩展未实现功能

本文档基于对 Node 扩展源码（`src/extensions/node/`）与 C 运行时（`runtime/ext_node/`）的逐文件核对整理，列出 **Node 扩展当前未实现 / 缺失** 的功能。

> 相关文档：
> - 核心语言能力见 [implemented.md](implemented.md) / [unimplemented.md](unimplemented.md)
> - Node 扩展已实现部分见 [node-implemented.md](node-implemented.md)

---

## 1. `fs` 模块未实现

当前 `fs` 仅实现 `readFileSync` / `readTextFile`（同一 C 符号），其余全部未实现：

| 未实现 API | 类别 |
| --- | --- |
| `readFile` | 异步读取 |
| `writeFileSync` / `writeFile` | 写入 |
| `appendFileSync` / `appendFile` | 追加写入 |
| `existsSync` / `exists` | 存在性检查 |
| `statSync` / `stat` / `lstatSync` / `fstatSync` | 文件元信息 |
| `readdirSync` / `readdir` | 目录读取 |
| `mkdirSync` / `mkdir` / `mkdtempSync` | 目录创建 |
| `rmSync` / `rmdirSync` / `unlinkSync` | 删除 |
| `renameSync` / `copyFileSync` / `linkSync` / `symlinkSync` | 重命名 / 复制 / 链接 |
| `truncateSync` / `chmodSync` / `chownSync` / `utimesSync` | 元数据修改 |
| `watch` / `watchFile` | 文件监听 |
| `openSync` / `closeSync` / `readSync` / `writeSync` | 文件描述符级操作 |
| `realpathSync` / `readlinkSync` | 路径解析 |

### 1.1 读取语义缺口

- 无 `encoding` 选项：始终按 UTF-8 文本返回，不支持 `'utf8'` / `'ascii'` / `'base64'` / `'hex'` 等参数。
- 无 `Buffer` / 二进制返回：`readFileSync` 无法返回 `Buffer`（xbintsc 目前没有 `Buffer` 值类型）。
- 错误处理与 Node 不符：打开失败只打印 stderr 并返回 `undefined`，不会抛出 `Error` / `ENOENT` 等异常（xbintsc 尚无可捕获异常体系）。

---

## 2. 其它 Node 模块完全未实现

以下 Node 内置模块没有任何对应扩展 / 内置函数：

| 模块 | 说明 |
| --- | --- |
| `path` | `join` `resolve` `dirname` `basename` `extname` 等 |
| `os` | `platform` `arch` `homedir` `cpus` 等 |
| `process` | 进程信息、`argv` `env` `exit` `cwd` 等 |
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

## 3. Node 全局对象 / 命名空间未实现

| 未实现 | 说明 |
| --- | --- |
| `process` 全局对象 | 无 `process.argv` / `process.env` / `process.cwd()` / `process.exit()` |
| `global` / `globalThis` | 无 |
| `__dirname` / `__filename` | 无 |
| `require` / `module` / `exports` | 无（xbintsc 无 CommonJS 模块运行时） |
| `Buffer` / `TypedArray` | 无二进制值类型 |
| 命名空间式调用 `fs.readFileSync(...)` | ✗ 不支持。内置函数只能以裸全局标识符调用，如 `readFileSync(...)`，不能写成 `fs.readFileSync(...)`（`fs` 这个对象并不存在） |
| `import { readFileSync } from "fs"` | ✗ 无法运行。`import` 语句本身在代码生成阶段报 `UnsupportedFeature`（见 [unimplemented.md](unimplemented.md)）；扩展内置函数是全局符号映射，与 import 无关 |

---

## 4. 异步 / 事件循环模型未实现

- 无回调 / Promise 异步模型：`readFile` 等异步 API 无运行时支撑（xbintsc 无事件循环、无 `Promise`）。
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
已实现：readFileSync、readTextFile（均映射 xt_node_read_text_file）

未实现（fs）：readFile、writeFile(Sync)、appendFile(Sync)、exists(Sync)、
              stat(Sync)、readdir(Sync)、mkdir(Sync)、rm(Sync)、rename(Sync)、
              copyFile(Sync)、watch、open/read/write/close、encoding 选项、Buffer 返回

未实现（其它模块）：path、os、process、crypto、http、net、child_process、
                    util、stream、events、buffer、url、querystring、zlib、
                    readline、worker_threads、fs/promises

未实现（全局/命名空间）：process、global/globalThis、__dirname、__filename、
                        require/module/exports、Buffer、fs.readFileSync(...) 命名空间调用

未实现（异步）：回调 / Promise / 事件循环 / setTimeout / async-await

未实现（平台）：Bun 扩展（仅设计示例）
```
