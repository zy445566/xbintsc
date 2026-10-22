# `fs` 模块

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/fs.md)

`fs` 现在实现了**同步** API 表面（文件、目录、描述符、元数据、链接、`cp`、`glob`、`Dir`、`constants`），`fs/promises` 将每个同步函数包进已 settle 的 Promise（见[已实现：`fs`](../../implemented/node/fs.md)与[已实现：`fs/promises`](../../implemented/node/fs-promises.md)）。其余仍未实现，因为 xbintsc 的 `fs` **没有异步 I/O 调度器后端**（核心事件循环已提供定时器与套接字）：

| 未实现 API | 类别 |
| --- | --- |
| `readFile` / `writeFile` / `appendFile` / `open` / `close` / `read` / `write` / `stat` / … | 回调式异步文件操作（`fs/promises` 提供 Promise 形式） |
| `createReadStream` / `createWriteStream` | 流式读写 |
| `watch` / `watchFile` | **真正的**文件监听：函数存在但返回对象从不触发（无 inotify/kqueue 后端） |
| `openAsBlob`、`statfs` 回调形式、`rmdir` 的 `maxRetries` / `retryDelay` | 需异步重试的杂项选项 |
| `glob` 的 `exclude` / `follow`、`cp` 的 `filter` | 选项回调 |

## 读取语义缺口

- `readFileSync` 支持编码选项（默认/`utf8`/`ascii`/`latin1`/`binary`/`hex`/`base64`/`base64url`），但默认**不返回 `Buffer`**：即使提供了 `Buffer`（以普通对象模拟），`readFileSync` 仍以字符串返回。要读取原始字节请显式指定。
- 未实现 `utf16le` / `ucs2` 解码（按文本返回字节）。
- `watch` / `watchFile` / `unwatchFile` 返回 API 形态的发射器对象，`.close()` / `.on()` 方法存在但**不会触发**事件。
- `Dir.read(cb)` / `Dir.close(cb)` 与 `FileHandle` 异步方法都会**同步**完成（回调/Promise 立即被调用）。
- `mkdtempSync` 总是向前缀追加 6 个随机字符（不要求以 `XXXXXX` 结尾）。
- `globSync` 支持 `*`、`?`、`[...]`、`**`，但不支持 `exclude` 回调与 `follow`；`**` 不跟随符号链接（与 Node 默认一致）。
- `cpSync` 的符号链接处理在常见场景下与 Node 一致，但不支持 `verbatimSymlinks`。
- 平台差异：Windows 上 `readlinkSync` 抛 `ENOSYS`，`chmodSync` / `lchmodSync` / `chownSync` / `lchownSync` / `fchmodSync` / `fchownSync` 为空操作；`statfsSync` 在 Windows/AIX/Sun 返回全零字段；`lutimesSync` 在 macOS/Windows 退化为 `utimesSync`。
