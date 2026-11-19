# C 运行时核心

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/runtime-core.md)

实现位置：`runtime/xt_alloc.c`、`runtime/xt_values.c`、`runtime/xt_containers.c`、
`runtime/xt_builtins.c`、`runtime/xt_constructors.c`、`runtime/xt_io.c`，共享私有头
`runtime/rt_internal.h`；公开 ABI 见 `runtime/rt.h`。

字符串、symbol、类型化数组、集合、Promise/异步、`fetch` 与标准库表面各有独立页面：

- [字符串与文本](./strings.md)
- [标准库](./stdlib.md)
- [集合](./collections.md)
- [类型化数组与二进制数据](./typed-arrays.md)
- [异步、Promise 与生成器](./async.md)
- [fetch](./fetch.md)

- 分配器：`xt_alloc` 背后的非移动标记-清扫回收器（显式根、子系统根提供者与保守 C 栈扫描）。bundle 的模块体执行期间会推迟回收（`xt_gc_begin_startup` 把阈值提到 512 MiB，直到 `xt_gc_end_startup`）：庞大的模块图全部在一个生成函数里构建，中途回收会破坏存活对象。`XT_GC_THRESHOLD` 仍然覆盖这两个值。下一个阈值是存活堆的 **4×**：交互式场景会长时间保持较大的存活集，2× 时 three.js 页面约每 1.2 秒回收一次；4× 把停止世界的停顿次数减半，同时阈值仍跟随（并随存活集缩小）存活堆，不会单向上涨。`XT_GC_TRACE=1` 会打印每次回收的堆、分配数与存活种类计数，以及按 table/mark/sweep 阶段拆分的 `took <ms>`。针对这类问题有两个环境开关：`XT_GC_VERIFY=1` 会在清扫前重新扫描每个已标记对象的 payload，打印出它引用但未被标记的块（标记器或根的缺口）；崩溃报告会指出落在“已被回收块”内的故障地址。I/O worker 线程会分配返回值但从不回收（`xt_gc_worker_thread`）：回收器只扫描主线程栈区间且不是线程安全的，而回收进行中新建的块会带上标记，避免被清扫掉。
- 小块分配：不超过 2 KiB 的块（大多数对象、闭包、字符串，以及它们背后的属性表 / 数组缓冲）不再逐个 `malloc`/`calloc`，而是从按尺寸分类的空闲链表分配，空闲链表再由 1 MiB slab 切分；回收器把块归还到对应类别，稳定状态下内存被复用，对象密集的场景构建不再为每个对象付一次 C 分配器往返。大块仍用 `malloc`/`free`，I/O worker 仍用 `calloc`（内存池仅限主线程），池化块带 `XT_GC_POOLED` 标记，清扫时能正确归还。
- 对大程序最常分配的种类采用**精确追踪**：`xt_object` 标记原型、错误栈与每个属性的 key/value/getter/setter，数组标记 items 与 `extra`，函数标记名字属性包、原型与环境，Map/Set/RegExp 标记元素数组；只有较少见的种类（迭代器、symbol）仍走保守 payload 扫描。`xt_gc_mark_value` 用一次 tag 比较就把所有数字排除在外，因此标记一个 40 万对象的场景不会把时间花在每个属性槽的五次 `XT_IS_*` 分支上。
- host 绑定背后的 C 字符串属性查找（`xt_object_get_cstr(w, "__xt_gl_context")`、类型化数组辅助函数、运行时自身的辅助函数）把键的**内容**驻留在一张可被 GC 看见的小表里，而不是每次调用分配一个 `xt_string`。这曾是 WebGL 页面的主要分配来源：Saltline 启动时约 2400 万次此类调用；加入表后同一轮运行约 16.5 万次调用、总共只分配约 19 万个字符串，垃圾量降低约两个数量级。
- 时间：`performance.now()` 返回相对**程序自身**时间原点的毫秒数（类似浏览器的 navigation start 或 Node 的进程启动），而不是原始单调计数器（Windows 上 QPC 从开机起算，进程若在开机六天后启动，第一次调用会返回六天）。生成的 `main` 通过 `xt_time_init()` 记录原点，动画帧回调收到的时间戳使用同一时钟。
- 性能诊断：`XT_PROFILE=1` 统计最热运行时辅助函数的进入次数（分配、回收、属性读写、方法分派、闭包调用、数组元素读取、类型化数组读写、`Math` 分派），退出时打印到 stderr；它还会按对象种类拆分分配次数，并列出最频繁的二十个 C 字符串属性名，无需采样分析器就能把分配压力归因到某个辅助函数或键。启用时每次只多一个分支，未设置时没有开销；配合同一辅助函数的微基准即可把耗时归因。
- 值构造：`xt_undefined/xt_null/xt_bool/xt_number/xt_string_new/xt_string_from_cstr`。
- 类型转换：`xt_truthy`、`xt_to_number`、`xt_to_string`、`xt_typeof`。
- 算术：`add/sub/mul/div/mod/pow/neg/pos`。
- 位运算：`and/or/xor/not/shl/shr/ushr`（含 `ToInt32` 语义）。
- 比较：`lt/le/gt/ge`、宽松 / 严格相等、`not`、`is_nullish`。
- 对象：线性属性列表，`object_new/get/set/has/keys/values/entries/assign/spread`。
- 数组：`array_new/get/set/push/length/spread`；对 `arr.length` 赋值会截断 / 扩展（与 JS 一致）；`iter_length` / `iter_value` 为数组、字符串（按码点）、`Map`、`Set` 提供统一迭代视图。
- 通用成员访问：`xt_get` / `xt_set`（对数组 / 对象 / 字符串分发）。
- 运算符辅助：`xt_in`（`in`）、`xt_delete`（`delete`）、`xt_rest_args`（剩余参数 / `arguments`）。
- Box：`box_new/get/set`（用于闭包捕获变量）。
- 函数与闭包：`arg`、`closure_new/call/env/arity`。
- 异常：`xt_try_enter` / `xt_try_exception` / `xt_try_leave` 维护 `_setjmp` 帧栈；`xt_throw` 在存在帧时长跳到最近 `try`，否则打印 `Uncaught ...` 后退出。无人观察的 promise rejection 同样是致命的：事件循环排空后 `xt_promise_report_unhandled` 会打印 `Unhandled promise rejection` 与原因，并以状态码 1 退出（见[异步](./async.md)）。codegen 还会维护一条影子调用栈（函数名、源文件、行号）：`Error.stack` 用它生成堆栈（见[标准库](./stdlib.md)），原生崩溃时致命信号处理器也会打印同一组帧。当 bundle 的模块体正在执行时，栈会在帧之上打印正在初始化的模块（`    module: <path>`）：所有模块共用一个生成函数，仅看帧只能拿到入口文件的坐标。设置 `XT_STACK_WATCHDOG_MS=<n>` 会启动一个采样线程：当程序连续 *n* 毫秒没有进入或退出任何函数时，就把这些帧打印出来，把「静默卡死」（意外的死循环、阻塞的宿主调用）变成具体源码位置；不设置该变量时不会启动线程。`--debug` 还会生成 `!DICompileUnit` / `!DISubprogram` / `!DILocation` 元数据，并用 `-g -fno-omit-frame-pointer` 编译，因此 gdb/lldb/WinDbg 可以把指令映射回源码行。`!DILocation` 的 `column` 是 16 位字段，压缩包里的列号可达数十万，因此会被钳制到 65535，而不是让 clang 直接拒绝整个模块；行号同样按 24 位上钳制。
- 回收器从结构体内部追踪的**类型化指针**（promise 的 reaction 列表、正在运行的协程帧、字符串 rope 的子节点）会先在堆表中校验再标记，失效指针被跳过而不是解引用（`XT_GC_VERIFY=1` 会报告）。
- 对象 / 函数：`xt_object` 带原型链，`xt_function` 带属性包（静态成员与 `prototype`）；`xt_new`（实例化）、`xt_instance_of`（原型链）、`xt_object_freeze/is_frozen/from_entries`；`xt_constructors.c` 保存一等公民构造器缓存、构造器原型与静态值分派。

