# 模块系统

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/modules.md)

实现位置：`src/parser/parser.ts`（语法）、`src/driver/modules.ts`（打包）、
`src/driver/frontend.ts`（`parseEntry`）。

## 语法

- `import default, { named } from "..."`、`import * as ns from "..."`（相对模块的命名空间导入会降级为合成对象字面量；扩展模块如 `path` 亦已支持）、`import type`
- `export default`、`export { a as b }`、`export *`、`export =`
- import attributes（`with` / `assert`）

## 运行时语义

- `import` / `export` 的**运行时语义**由 `src/driver/modules.ts` 在驱动层完成：递归解析相对依赖**以及裸名称的 `node_modules` 包**（遵循 `exports` / `module` / `main` 与包子路径），按模块前缀重命名顶层符号、改写引用，合并为单文件后重新绑定。
- 包可以是 ESM 或 CommonJS：`node_modules` 内的 CJS 文件会获得合成的 `module` / `exports`，`require` 调用被降级为依赖的导出对象，静态识别出的导出对 ESM 导入方可见；`node_modules` 之外的 `require()` 仍报错并提示改用 `import`。循环依赖报错。
- `.js` 系列说明符解析到对应 `.ts` 源码；省略后缀时优先 TypeScript 后缀而非同名的 JavaScript 文件；纯 `.js` / `.jsx` / `.mjs` / `.cjs` 源码也可直接打包。
- 导入绑定与命名空间成员是模块求值时的快照（无实时绑定）—— 见[未实现：模块系统](../unimplemented/modules.md)。

## import.meta

- `import.meta.url` / `.dirname` / `.filename`，由正在运行的可执行文件路径解析。

## 扩展模块

- 扩展模块（如 `fs`）可通过裸名称或 `node:` 前缀的 `import` 引入，并解析到运行时入口；见[扩展机制](./extensions.md)。
