# `fs` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/fs.md)

实现位置：`src/extensions/node/fs/*.ts`、`runtime/ext_node/fs/*.c`

## 可用函数（从 `fs` 导入）

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
| `watch(filename[, options][, listener])` | `xt_node_watch` | 返回发射器形态的 watcher（不会触发，见[未实现：`fs`](../../unimplemented/node/fs.md)） |
| `watchFile(filename[, options], listener)` | `xt_node_watch_file` | 轮询形态的 `StatWatcher`（不会触发） |
| `unwatchFile(filename[, listener])` | `xt_node_unwatch_file` | 停止监听 |
| `constants` | `xt_fs_constants` | `F_OK` / `R_OK` / `W_OK` / `X_OK`、`COPYFILE_*`、`O_*`、`S_IF*`（宿主值） |
| `promises` | `xt_fs_promises` | `fs/promises` 门面（见 [`fs/promises`](./fs-promises.md)） |

## 编码支持

`readFileSync` / `writeFileSync` / `appendFileSync` 的 `options` 可以是编码字符串，也可以是 `{ encoding: "..." }`：

| 编码 | 读取 | 写入 |
| --- | --- | --- |
| 默认 / `utf8` / `utf-8` / `ascii` / `latin1` / `binary` | 原始 UTF-8 文本 | 按文本字节写入 |
| `hex` | 小写十六进制字符串 | 解析十六进制后写入 |
| `base64` | Base64 字符串 | 解析 Base64 后写入 |
| `base64url` | Base64url 字符串 | 解析 Base64url 后写入 |
| `utf16le` / `ucs2` | 按文本直接读取原始字节（不做 UTF-16 解码） | 按 UTF-8 文本写入 |

> `readFileSync` 默认仍返回**字符串**而不是 `Buffer`（即使 `Buffer` 类已存在），这样 `console.log(readFileSync(p))` 仍打印文本。

## `statSync` 返回结构

返回普通对象，数值属性：`size`、`mode`、`uid`、`gid`、`dev`、`ino`、`nlink`、`rdev`、`blksize`、`blocks`、`atimeMs`、`mtimeMs`、`ctimeMs`、`birthtimeMs`。
方法（原生闭包，可调用）：`isFile()`、`isDirectory()`、`isSymbolicLink()`、`isFIFO()`、`isSocket()`、`isBlockDevice()`、`isCharacterDevice()`。

## 调用方式

`fs` 的导出通过 `import` 引入（也可用 `node:fs` 别名），不再以裸全局标识符暴露：

```ts
import { readFileSync, writeFileSync, existsSync } from "fs";

const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

## 错误处理

失败时函数会**抛出** Node 形态的 `Error` 对象，带有 `name`（`Error`）、`message`、`code`（如 `ENOENT`）、`errno`（数值 `errno`）、`syscall` 与 `path`。`existsSync` 仍返回布尔值，不会抛出。

```ts
import { readFileSync } from "fs";
try {
  readFileSync("/does/not/exist");
} catch (error) {
  console.log((error as any).code); // ENOENT
}
```


已知偏差见[未实现：`fs`](../../unimplemented/node/fs.md)。
