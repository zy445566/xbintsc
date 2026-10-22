# 工具链、平台与工程

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/toolchain.md)

| 项 | 现状 |
| --- | --- |
| GC（垃圾回收） | ✓ `xt_alloc.c` 中的非移动标记-清扫，按存活堆阈值（`XT_GC_THRESHOLD` 可固定该阈值）或 `xt_gc_collect()` 触发；每次回收后阈值变为存活堆的 2 倍（下限 1 MiB），因此常驻内存跟随存活集而不是只增不减；`XT_GC_TRACE=1` 会为每次回收向 stderr 打印一行统计。显式根 + 根提供者 + 保守活动栈扫描。`WeakMap`/`WeakSet` 的键是 ephemeron（只有键可达时才保留值），`WeakRef` 目标不可达时会被清空；由于栈扫描是保守的，残留的栈字可能延迟清空。无压缩或后台回收 |
| 自举（self-hosting） | ✓ 编译器已能自编译：`xbintsc build src/cli/main.ts` 可产出可用二进制，且从第 1 代起产出的 IR 保持稳定。运行时仍为 C |
| 类型检查器 | ✗ 仅定义诊断码，无 checker |
| 完整标准库 | 部分：Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` / `Symbol` / `structuredClone` 已实现；String.normalize 缺失 |
| 多文件模块打包 | 部分：相对路径 `.ts` 打包、ESM `node_modules` 包、命名空间导入、裸说明符扩展模块导入已实现；CommonJS 仅在 `node_modules` 内处理；循环依赖 / 实时绑定未实现 |
| 异步运行时 | ✓ `async`/`await` 在 `select(2)` reactor 上对 pending promise 挂起：定时器、TCP 套接字、offload 的文件/HTTP 工作与 promise 回调都能恢复协程。仍缺：真正的 OS 级文件监听、非阻塞 `net.connect`、`fetch` 的 TLS，以及用户可见的 worker 线程（`worker_threads` 仍用子进程） |
| Windows 二进制产物验证 | 构建层已适配（`.exe` 后缀、链接参数分支），并在 CI 中验证 |
| 精确的 ECMAScript 数值 / 字符串 / 比较语义 | 部分，见[运行时偏差](./deviations.md) |
