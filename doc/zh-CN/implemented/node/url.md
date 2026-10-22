# `url` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/url.md)

实现位置：`src/extensions/node/url/index.ts`、`runtime/ext_node/url/url.c`

- `pathToFileURL(path)` 与 `fileURLToPath(url)`。
- 这两个助手用于在 `import.meta.url`、`process.argv[1]` 与文件系统路径之间转换。
