# WebAssembly

> 属于[已实现语法与特性](../implemented.md) · [English](../../implemented/wasm.md)

位置：`src/extensions/wasm.ts`（链接特性）、`runtime/xt_wasm/`（桥接与 API）、
`runtime/xt_webassembly.c`（核心命名空间桩）、`vendor/wasm3/`（解释器，由
`npm run vendor` 获取）。

## `wasm` 链接特性

`WebAssembly` 是与 `Math`、`crypto` 类似的一等命名空间对象，因此
`const W = WebAssembly` 与 `typeof WebAssembly` 都能工作。解释器不会链入每个
程序：用法扫描在打包源码中出现 `WebAssembly` 时选择 **`wasm` 链接特性**，
这时才编译固定版本的 wasm3 源码和桥接层。`--feature wasm` 可强制启用（通过
字符串访问 `globalThis["WebAssembly"]` 的程序需要它），`--no-feature wasm`
则保持不链入；此时命名空间成员在运行时抛出
`WebAssembly is not supported in this build`。

该特性是 *required* 的：缺少 `vendor/wasm3` 时构建直接失败并提示运行
`npm run vendor`，而不是警告后生成一个带未解析符号的二进制。

## 支持的 API

| 接口 | 行为 |
| --- | --- |
| `WebAssembly.validate(bytes)` | 解析/校验模块；非法字节码返回 `false` |
| `WebAssembly.compile(bytes)` | `Promise<Module>`；非法字节码以 `CompileError` 拒绝 |
| `WebAssembly.instantiate(bytes, imports?)` | `Promise<{ module, instance }>` |
| `WebAssembly.instantiate(module, imports?)` | `Promise<Instance>` |
| `new WebAssembly.Module(bytes)` | 同步编译；失败抛 `CompileError` |
| `new WebAssembly.Instance(module, imports?)` | 同步实例化；失败抛 `LinkError` |
| `WebAssembly.Module.exports(module)` / `.imports(module)` | 描述符数组（`{ name, kind }` / `{ module, name, kind }`） |
| `WebAssembly.Module.customSections(module, name)` | 返回 `[]`（暂不暴露自定义段） |
| `new WebAssembly.Memory({ initial, maximum? })` | 独立的页式 `ArrayBuffer`，带 `buffer` 与 `grow()` |
| `instance.exports.<function>` | 可从 JS 调用；`i32`/`f32`/`f64` 为 number，`i64` 为 BigInt |
| `instance.exports.<memory>` | `.buffer` 是 `ArrayBuffer` 快照，`.grow(pages)` 增长 wasm 内存 |
| JS 函数导入 | 按声明的数值类型从 wasm 调用；JS 抛出的异常会传回 wasm 调用方 |
| `CompileError` / `LinkError` / `RuntimeError` | 真正的 `Error` 子类；`instanceof WebAssembly.CompileError` 与 `instanceof Error` 均成立 |

`WebAssembly.compile` 与 `instantiate` 都通过普通 Promise 队列结算，因此
`await` 可用，等待期间定时器和套接字仍会继续运行。

### 内存在调用边界同步

运行时的 `ArrayBuffer` 是 JavaScript 堆上的字节数组，不是 wasm3 线性内存的
实时别名。`instance.exports.<memory>.buffer` 会把 wasm 字节复制出来，每次调用
导出函数前把 JS 缓冲区复制进 wasm 内存、调用后再复制回来。因此通过 typed
array 的读写对下一次 wasm 调用可见（反向亦然），但视图不是零拷贝窗口：写入
要到下一次调用或读取 `.buffer` 时才被观察到。

### `.wasm` 导入

核心扩展安装了二进制资源加载器，因此可以直接导入 wasm 文件：

```ts
import bytes from "./add.wasm";

const { instance } = await WebAssembly.instantiate(bytes, {});
console.log(instance.exports.add(2, 3));
```

字节会嵌入可执行文件（生成模块中的 base64），构建后的程序运行时不再需要
`.wasm` 文件。

## 限制

- 只桥接**函数**导入。导入 memory、table 或 global 的模块会以 `LinkError`
  拒绝。
- 导出的 **table** 与 **global** 会出现在 `Module.exports` 中，但不会包装到
  `instance.exports`；目前只包装函数和内存。
- 未实现 `WebAssembly.Table` 与 `WebAssembly.Global` 构造器。
- 未暴露 `compileStreaming` / `instantiateStreaming`。
- 执行能力受 wasm3 指令集限制（无 SIMD 指令、无 GC、无宿主线程）；解析器接受
  固定版本 wasm3 启用的提案，遇到不支持的操作码会在编译/实例化模块时以
  WebAssembly 错误失败。

跟踪中的缺口见[未实现：WebAssembly](../unimplemented/wasm.md)。
