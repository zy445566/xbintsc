# CLI 与编程接口

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/cli.md)

实现位置：`src/cli/main.ts`、`src/index.ts`、`bin/xbintsc.js`

## CLI 命令

```
xbintsc build <file.ts> [options]   编译为原生二进制
xbintsc run   <file.ts> [-- args]   编译并执行
xbintsc check <file.ts>             解析、绑定并检查，不链接
xbintsc watch <file.ts>             源码树变化时重新构建
xbintsc emit  <file.ts>             打印 LLVM IR
xbintsc doctor                      报告解析到的工具链
xbintsc version                     打印版本
xbintsc help                        帮助
```

`check`、`emit`、`build`、`run` 与 `watch` 默认都会运行语义检查；
`--no-check` 可跳过（该开关也进入缓存 key）。详见[语义检查](./checker.md)。

## CLI 选项

```
-o, --output <path>   输出路径
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0..-O3              优化级别（默认 -O2）
    --ext <names>     逗号分隔扩展（如 node）
    --feature <names> 强制开启可选链接特性（media,gpu,all）
    --no-feature <n>  即使检测到也不链接该可选特性
    --ext-native <m>  从 JSON manifest 注册 C++/Rust 扩展
    --config <path>   使用项目配置（默认自动查找 xbintsc.config.json）
    --no-config       不读取任何项目配置
    --icon <path>     内嵌应用图标（PNG/ICO/ICNS）
    --bundle          macOS：同时生成 .app bundle
    --app-name <name> bundle / 显示名称
    --app-id <id>     macOS bundle 标识符（如 com.example.demo）
    --force           忽略增量缓存
    --verbose         打印进度信息
    --debug           生成 DWARF/CodeView 调试信息
    --check           运行语义检查（默认）
    --no-check        跳过语义检查
```

## 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir, diagnostics } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
// 检查器可以按调用关闭，等价于 `--no-check`：
build("program.ts", { check: false });
```

选项与结果类型（`BuildOptions`、`BuildResult`、`EmitKind`、`PhaseTiming`）位于
`src/driver/options.ts`，并由 `src/driver/compiler.ts` / `xbintsc` 重新导出。
