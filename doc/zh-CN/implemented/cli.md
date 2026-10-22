# CLI 与编程接口

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/cli.md)

实现位置：`src/cli/main.ts`、`src/index.ts`、`bin/xbintsc.js`

## CLI 命令

```
xbintsc build <file.ts> [options]   编译为原生二进制
xbintsc run   <file.ts> [-- args]   编译并执行
xbintsc emit  <file.ts>             打印 LLVM IR
xbintsc version                     打印版本
xbintsc help                        帮助
```

## CLI 选项

```
-o, --output <path>   输出路径
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0..-O3              优化级别（默认 -O2）
    --ext <names>     逗号分隔扩展（如 node）
    --ext-native <m>  从 JSON manifest 注册 C++/Rust 扩展
    --config <path>   使用项目配置（默认自动查找 xbintsc.config.json）
    --no-config       不读取任何项目配置
    --icon <path>     内嵌应用图标（PNG/ICO/ICNS）
    --bundle          macOS：同时生成 .app bundle
    --app-name <name> bundle / 显示名称
    --app-id <id>     macOS bundle 标识符（如 com.example.demo）
    --force           忽略增量缓存
    --verbose         打印进度信息
```

## 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

选项与结果类型（`BuildOptions`、`BuildResult`、`EmitKind`、`PhaseTiming`）位于
`src/driver/options.ts`，并由 `src/driver/compiler.ts` / `xbintsc` 重新导出。
