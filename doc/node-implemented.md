# xbintsc Node 扩展已实现功能

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
  fs/write-file.ts                 path/path.c
  fs/fs-ops.ts                     os/os.c
  path/index.ts                    process/process.c
  os/index.ts
  process/index.ts
```

- `NodeModule` 接口：
  - `name`：模块名（如 `fs`）
  - `runtimeSources()`：该模块的 C 源
  - `builtins()`：全局标识符 → 运行时符号的映射（`path` / `os` / `process` 通过命名空间分发，故返回空表）
- `resolveFrom(importMetaUrl, relative)`：把相对于当前模块目录的路径解析为绝对路径，用于定位 C 源。

---

## 2. `fs` 模块（已实现，仅同步 API）

实现位置：`src/extensions/node/fs/*.ts`、`runtime/ext_node/fs/*.c`

### 2.1 可用函数（裸全局标识符）

| 全局函数 | 运行时符号 | 说明 |
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

`fs` 内置函数以**裸全局标识符**形式暴露（不是 `fs.readFileSync(...)`）：

```ts
const text = readFileSync("examples/data.txt");
writeFileSync("/tmp/out.txt", text);
console.log(existsSync("/tmp/out.txt"));
```

### 2.5 错误处理

打开 / 操作失败时向 stderr 打印 `xbintsc: cannot ... 'path'` 并返回 `undefined`（`existsSync` 返回 `false`），不会抛出 `Error` / `ENOENT` 异常（xbintsc 尚无可捕获异常体系）。

---

## 3. `path` 模块（已实现）

实现位置：`src/extensions/node/path/index.ts`、`runtime/ext_node/path/path.c`

采用 `path.<name>(...)` 命名空间调用，编译器将其降为 `xt_path_static(<name>, argc, argv)`。语义为 POSIX（`/` 分隔符）。

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
console.log(path.join("a", "b", "..", "c")); // a/c
console.log(path.basename("/x/y/z.txt"));    // z.txt
```

---

## 4. `os` 模块（已实现）

实现位置：`src/extensions/node/os/index.ts`、`runtime/ext_node/os/os.c`

采用 `os.<name>(...)` 命名空间调用，降为 `xt_os_static(<name>, argc, argv)`。

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

## 6. 已实现 Node 能力速查

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
| 调用约定 | 统一 `(argc, argv)` ABI，返回 `xt_value` |
| 链接方式 | 注册后编译 `runtime/ext_node/**` 并随运行时一起链接 |
