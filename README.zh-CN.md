# xbintsc

> 语言 / Language：**简体中文** | [English](./README.md)

`xbintsc` 把 **TypeScript 的一个实用子集直接编译为原生二进制**。
它自身解析 TypeScript，进行名字绑定，把程序下降为 **LLVM IR 文本**，
再把 IR 交给 **clang**，链接一个小型 C 运行时，产出独立可执行文件。

设计目标是：

1. **增量编译** —— 构建以内容寻址，没有变化时直接跳过。
2. **模块划分清晰** —— 词法、语法、绑定、代码生成、驱动、扩展各自独立目录，并有各自的测试。
3. **测试充分** —— 每个模块单元测试，外加真正编译并运行二进制的端到端测试。
4. **可插拔扩展** —— Node `fs`、Bun API 等是可选的编译模块，而非核心代码。
5. **为自举做准备** —— 编译器用 TypeScript 编写并输出 IR 文本，因此用 TypeScript 重写的版本最终可以编译自身。
6. **多平台** —— 在多种 CPU 上支持 macOS、Linux 与 Windows，并由 GitHub Actions 验证。

## 工作原理

```
source.ts
   │  词法分析  src/lexer
   ▼
 tokens
   │  语法分析  src/parser        → AST (src/ast)
   ▼
 AST
   │  名字绑定  src/binder        → 作用域、符号、闭包
   ▼
 bound AST
   │  代码生成  src/codegen       → LLVM IR 文本 (src/codegen/llvm.ts)
   ▼
 module.ll ──clang──► module.o ──链接──► 可执行文件
                                  ▲
                          runtime/（C 运行时，NaN-boxing 值）
```

### 值模型

JavaScript 值统一为一个 64 位字（`xt_value`）。double 不做装箱直接存放；
其它类型是带标签指针：高 16 位 tag 加 48 位 payload。该表示法在
`src/codegen/values.ts` 与 `runtime/rt.h` 中定义一次，编译器与运行时共享。

### 调用约定

所有编译期函数使用统一 ABI：

```c
xt_value fn(xt_value env, int32_t argc, xt_value *argv);
```

`env` 通过 box 以引用方式线程化捕获变量，因此直接调用与闭包调用共用同一条
代码路径。不易内联的 JavaScript 语义（`+` 隐式转换、关系比较、属性访问、打印）
委托给 `@xt_*` 运行时调用。

### 运行时

`runtime/` 实现字符串、对象、数组、闭包、算术、比较、异常与类 Node 的
`console.log` 打印。C 代码按功能拆分到多个翻译单元（`xt_alloc.c`、`xt_values.c`、
`xt_containers.c`、`xt_stdlib.c`、`xt_builtins.c`、`xt_io.c`），共享私有头
`runtime/rt_internal.h`。它使用 bump arena 且永不释放 —— 垃圾回收被有意推迟并
隔离在 `xt_alloc` 之后，因此可以在不改动编译器的前提下替换。

## 使用方法

有两种调用 CLI 的方式：

- **作为用户** —— 安装包后即可使用 `xbintsc` 命令（通过 `bin` 条目 `./bin/xbintsc.js`）。
- **作为开发者** —— 在源码检出中直接运行，无需先构建。

### 作为用户

```bash
# 全局安装
npm install -g xbintsc

# ...或按需使用，无需安装
npx xbintsc version

# 编译并运行程序
xbintsc run examples/hello.ts

# 产出原生二进制
xbintsc build examples/hello.ts --out build/examples
./build/examples/hello

# 查看生成的 LLVM IR
xbintsc emit examples/hello.ts | head

# 使用可选扩展（这里为 Node 的 fs，通过 import 引入）
xbintsc run examples/read-file.ts --ext node
```

### 作为开发者（从源码）

在源码检出中，通过 `tsx` 直接运行 TypeScript 源码（或使用 `npm run xbintsc`）。
`bin` 启动器在这里同样可用：没有 `dist/` 构建时会自动回退到 `tsx`。

```bash
# 安装依赖
npm install

# 编译并运行程序
npm run xbintsc -- run examples/hello.ts
# 等价于
npx tsx src/cli/main.ts run examples/hello.ts

# 产出原生二进制
npx tsx src/cli/main.ts build examples/hello.ts --out build/examples
./build/examples/hello

# 查看生成的 LLVM IR
npx tsx src/cli/main.ts emit examples/hello.ts | head

# 使用可选扩展（这里为 Node 的 fs，通过 import 引入）
npx tsx src/cli/main.ts run examples/read-file.ts --ext node
```

CLI 选项：

```
-o, --output <path>   输出路径
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0..-O3              优化级别（默认 -O2）
    --ext <names>     逗号分隔扩展（如 node）
    --force           忽略增量缓存
    --verbose         打印进度信息
```

### 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

## 增量编译

驱动以入口源码哈希、编译器版本、编译选项、平台与启用的扩展集合作为每次构建的
键（`src/driver/cache.ts`）。如果所有记录的产物仍然存在，构建立即返回。C 运行时
与扩展源按同样的原则只编译一次并缓存。

## 扩展

扩展是一个普通对象（`src/extensions/registry.ts`）。`node` 扩展本身按每个 Node
模块拆分目录，每个模块把它对外导出的绑定与实现它们的 C 源配对：

```
src/extensions/node/       runtime/ext_node/
  index.ts   # nodeExtension  fs/read_file.c   fs/write_file.c   fs/fs_ops.c
  fs/index.ts                path/path.c      os/os.c           process/process.c
  fs/read-file.ts
  path/index.ts
  os/index.ts
  process/index.ts
```

```ts
// src/extensions/node/index.ts
const modules: readonly NodeModule[] = [fsModule, pathModule, osModule, processModule];

export const nodeExtension: Extension = {
  name: "node",
  runtimeSources: () => [...new Set(modules.flatMap((m) => m.runtimeSources()))],
  modules: () => Object.fromEntries(
    modules.flatMap((m) => {
      const entry = { namespace: m.namespace, exports: m.exports?.() ?? m.builtins() };
      return [[m.name, entry], [`node:${m.name}`, entry]];
    }),
  ),
};
```

注册它会链接额外的 C 源，并通过 `import` 暴露 Node API：
`import { readFileSync } from "fs"` 会解析到具有统一 `(argc, argv)` 调用约定的 C
符号。`path`、`os` 与 `process` 还会接入命名空间分发，因此
`import path from "path"`（或 `import * as path from "path"`）会让 `path.join(...)`
与 `process.cwd()` 下降为各自的运行时入口。新增一个模块意味着在
`src/extensions/node/` 下放入一个目录、并在 `runtime/ext_node/` 下放入对应的 C
实现；核心编译器永不改动。

Node 模块覆盖情况：

- [Node 扩展：已实现](./doc/zh-CN/node-implemented.md)
- [Node 扩展：未实现](./doc/zh-CN/node-unimplemented.md)

## 测试

```bash
npm run typecheck   # tsc --noEmit
npm test            # 单元 + 端到端（存在 clang 时运行真实二进制）
npm run test:e2e    # 仅编译并运行的测试
```

测试在 `tests/` 下按模块组织（`lexer`、`parser`、`binder`、`codegen`、`driver`、
`extensions`、`cli`、`e2e`）。

## 运行要求

- Node.js 20+
- `PATH` 中有兼容 `clang` 的 C 编译器（可用 `xbintsc_CLANG` 覆盖）

## 语言子集

- [已实现特性](./doc/zh-CN/implemented.md)
- [未实现特性](./doc/zh-CN/unimplemented.md)
