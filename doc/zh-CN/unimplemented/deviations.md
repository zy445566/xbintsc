# 运行时偏差

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/deviations.md)

这些特性**能编译、能运行**，但结果不完全符合 ECMAScript：

| 项 | 偏差 |
| --- | --- |
| 正则按字节匹配 | 引擎按 UTF-8 字节匹配：多字节字面量仍可匹配，但 `.` 与字符类每次只消费一个字节（`\d`/`\w` 仍是 ASCII）。`exec` / `search` / 替换回调返回的偏移会转换为 UTF-16 码元 |
| 全局正则 `lastIndex` | `/g`、`/y` 正则的 `test` / `exec` 不推进也不读取调用方设置的 `lastIndex` |
| `String.normalize` | 未实现 |
| 内置方法一等公民 | 以未绑定方法值实现（见[函数](./functions.md)）；`fn.toString()` 返回占位字符串而非源码文本 |
| `for...in` | 枚举对象 / 数组 / 字符串的自身键；不含原型链属性 |
| 数组越界 / 稀疏 | 越界访问返回 `undefined`；对 `arr.length` 赋值会截断 / 扩展，但不区分稀疏空洞 |
| 内存管理 | 非移动标记-清扫回收器（在 `xt_alloc` 之后）：显式根、子系统根提供者与保守 C 栈扫描。单线程、stop-the-world；无移动/压缩与分代 |
| 函数 `arity` / 调用参数个数 | 对本地可见函数的直接调用由默认[语义检查](../implemented/checker.md)校验（`TS4004`）；通过属性、导入、展开实参或重新赋值后的绑定发起的调用不校验。`fn.length` 报告声明的 arity |
| `async` / `await` | 已在栈式协程上真实挂起；`await` 总会排入一次微任务（非 Promise 与已 settle 的 Promise 也一样），因此与 pending `.then` 回调的交错顺序符合规范。顶层 `await` 会阻塞事件循环等待，而不是让出模块主体。`fs/promises` 与 `fetch` 把阻塞工作 offload 到 4 线程池并在 reactor 上 settle。异步生成器可在 `await` 与 `yield` 处挂起，`for await...of` 每次迭代等待一步 |
| 异步生成器清理 | `for await...of` 提前退出时不会调用迭代器的 `return()`。异步生成器中的 `yield*` 只委托同步可迭代对象。在挂起的 `yield` 处调用 `.return()` 会直接完成而不执行 `finally`（与同步生成器相同的限制） |
| `super` | `super.x` / `super(...)` 取 `this` 原型的原型；单级继承正确，继承深度 > 1 时可能不准确 |
| `Error.stack` | 仅函数级帧：每帧为 `at 名称 (文件:行)`，行号取自函数声明（最内层帧会更新为错误构造处）；没有列号，生成器 / `async` 函数体不产生帧（它们会切换栈） |
| 模块实时绑定 | 命名空间导入与导入绑定为快照（见[模块系统](./modules.md)） |
| `import.meta` | ✓ `import.meta.url` / `.dirname` / `.filename`，由可执行文件路径解析 |

> 数字格式化、宽松相等、`+` / 关系运算 `ToPrimitive`、整链可选链、`finally` 提前退出、对象键顺序等均已对齐 Node，并由差分测试覆盖，故不再列为偏差。
