# 模块系统

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/modules.md)

| 功能 | 现状 |
| --- | --- |
| 多文件 / 模块解析与链接 | ✓ 驱动层 AST 打包（`src/driver/modules.ts`）：解析每个模块、按模块前缀重命名顶层符号、改写引用后合并为单文件重新绑定 |
| 具名导入导出 | ✓ `import { a, b as c }` / `export { a as b }` / `export const/let/var/function/class` |
| 默认导入导出 | ✓ `export default` / `import d from` |
| 再导出 `export { x } from` / `export * from` | ✓（`export *` 复制依赖模块的导出） |
| 命名空间导入 `import * as ns` | ✓ 降级为持有全部导出的合成对象字面量 |
| 循环依赖 | ✗ 直接报错（不做循环初始化语义） |
| 实时绑定（live bindings） | ✗ 命名空间对象与导入绑定是模块求值时的快照 |
| ESM `node_modules` 包 | ✓ 裸说明符沿目录树解析，支持 `exports` / `module` / `main`、作用域包与子路径；包源码与相对模块一样被打包 |
| CommonJS `require` / `module.exports` | ✓ 仅限 `node_modules` 内：打包器为每个 CommonJS 包注入合成的 `module` / `exports`，将 `require("x")` 降级为依赖的导出对象，并把外部/内置 `require` 提升为 ESM `import`。用户代码（`node_modules` 之外）的 `require()` 仍会报错并提示改用 ESM `import` |

> 扩展模块（如 `fs`）可通过裸名称或 `node:` 前缀的 `import` 引入 —— `import { readFileSync } from "fs"` / `import path from "path"` —— 并解析到运行时入口（具名、默认与命名空间形式均可）。相对模块与 `node_modules` 中的 ESM/CJS 包均在驱动层打包。

这份契约的已实现部分见[模块系统](../implemented/modules.md)。
