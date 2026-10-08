# 面向 AI 的 xbintsc 使用指南

**xbintsc 把 TypeScript 的一个子集直接编译成原生二进制。** 它自己解析
TypeScript，把程序降级为 LLVM IR 文本，再调用 `clang` 链接一个小型 C 运行时
（`runtime/`）产出独立可执行文件。产物里没有 Node.js，也没有 TypeScript 编译器。

```
source.ts --词法分析--> tokens --语法分析--> AST --绑定--> 绑定后的 AST
          --代码生成--> module.ll --clang--> module.o --链接--> 可执行文件
```

本目录是**面向 AI、按任务组织**的文档层：刻意写得短，并指向更权威的文档。
请只读你当前任务需要的那一篇，不要通读整个目录。

| 页面 | 什么时候读 |
| --- | --- |
| [build-recipe.md](./build-recipe.md) | 要编译或运行一个程序，需要确切的命令形状 |
| [cli.md](./cli.md) | 需要完整参数表、项目配置或编程 API |
| [language-support.md](./language-support.md) | 必须确认某个语法/API 是否存在，或是否与 Node 一致 |
| [extensions.md](./extensions.md) | 需要 `fs`/`http`/…、GUI，或 C++/Rust 库 |
| [troubleshooting.md](./troubleshooting.md) | 报错了，要定位原因并修好 |

如果你要改的是**这个仓库本身**（而不是用它编译程序），先读
[../../AGENTS.md](../../../AGENTS.md)：那里讲了目录结构、构建门禁与改动规则。

## 三个决定一切的事实

1. **只有 TypeScript 的一个子集能编译。** 不支持的语法会被直接拒绝，而不是
   近似实现。动手写大段代码之前先确认。
2. **类型全部被擦除，从不做类型检查。** 类型注解、接口、泛型、`as`/`satisfies`、
   非空断言 `!` 在运行时没有任何作用，也没有类型检查器会跑。只有真实的运行时
   行为才算数。
3. **Node 兼容是可选且不完整的。** Node 模块由 `node` 扩展提供（`--ext node`）；
   不支持直接 import 第三方 npm 包。

## 选一条命令

```bash
# 直接运行程序（先编译成临时二进制，再执行）
xbintsc run app.ts

# 产出独立二进制
xbintsc build app.ts --out build          # -> build/app[.exe]

# 查看会被编译的 LLVM IR
xbintsc emit app.ts

# 查看 xbintsc 解析到的工具链
xbintsc doctor
```

在源码检出里，把 `xbintsc` 换成 `npx tsx src/cli/main.ts`（或
`npm run xbintsc --`）。发布压缩包里的二进制不需要 Node.js。

下一步：要命令就[build-recipe.md](./build-recipe.md)，要写正经程序就先看
[language-support.md](./language-support.md)。
