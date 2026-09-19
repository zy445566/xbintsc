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
  index.ts   # nodeExtension     fs/read_file.c
  module.ts  # NodeModule 接口
  fs/index.ts
  fs/read-file.ts
```

- `NodeModule` 接口：
  - `name`：模块名（如 `fs`）
  - `runtimeSources()`：该模块的 C 源
  - `builtins()`：全局标识符 → 运行时符号的映射

- `resolveFrom(importMetaUrl, relative)`：把相对于当前模块目录的路径解析为绝对路径，用于定位 C 源。

---

## 2. `fs` 模块（已实现）

实现位置：`src/extensions/node/fs/index.ts`、`src/extensions/node/fs/read-file.ts`、`runtime/ext_node/fs/read_file.c`

### 2.1 可用函数

| 全局函数 | 运行时符号 | 说明 |
| --- | --- | --- |
| `readFileSync(path)` | `xt_node_read_text_file` | 同步读取文本文件，返回字符串 |
| `readTextFile(path)` | `xt_node_read_text_file` | `readFileSync` 的别名（同一符号） |

### 2.2 调用方式

内置函数以**裸全局标识符**形式暴露（不是 `fs.readFileSync(...)`）：

```ts
const text = readFileSync("examples/data.txt");
console.log("file says:", text);
```

### 2.3 运行时语义

实现位置：`runtime/ext_node/fs/read_file.c`

- 统一 ABI：`xt_value fn(int32_t argc, xt_value *argv)`
- `argv[0]` 转为字符串作为路径（`xt_to_string` + `xt_string_data`）
- 以 `"rb"` 二进制模式 `fopen` 打开
- 读取整个文件内容，返回 `xt_string_new` 字符串
- 行为细节：
  - `argc < 1` → 返回 `undefined`
  - 打开失败 → 向 stderr 打印 `xbintsc: cannot open '...'` 并返回 `undefined`
  - 内容按 UTF-8 文本返回（无编码参数，无 `Buffer`）

---

## 3. 已实现 Node 能力速查

| 类别 | 内容 |
| --- | --- |
| 扩展注册 | `nodeExtension`（`--ext node`）、`NodeModule` 接口、`resolveFrom` 工具 |
| fs 读取 | `readFileSync`、`readTextFile`（均映射 `xt_node_read_text_file`） |
| 调用约定 | 统一 `(argc, argv)` ABI，返回 `xt_value` |
| 链接方式 | 注册后编译 `runtime/ext_node/fs/read_file.c` 并随运行时一起链接 |
