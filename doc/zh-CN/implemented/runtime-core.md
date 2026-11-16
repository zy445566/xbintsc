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

- 分配器：`xt_alloc` 背后的非移动标记-清扫回收器（显式根、子系统根提供者与保守 C 栈扫描）。bundle 的模块体执行期间会推迟回收（`xt_gc_begin_startup` 把阈值提到 512 MiB，直到 `xt_gc_end_startup`）：庞大的模块图全部在一个生成函数里构建，中途回收会破坏存活对象。`XT_GC_THRESHOLD` 仍然覆盖这两个值。针对这类问题有两个环境开关：`XT_GC_VERIFY=1` 会在清扫前重新扫描每个已标记对象的 payload，打印出它引用但未被标记的块（标记器或根的缺口）；崩溃报告会指出落在“已被回收块”内的故障地址。I/O worker 线程会分配返回值但从不回收（`xt_gc_worker_thread`）：回收器只扫描主线程栈区间且不是线程安全的，而回收进行中新建的块会带上标记，避免被清扫掉。
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
- 异常：`xt_try_enter` / `xt_try_exception` / `xt_try_leave` 维护 `_setjmp` 帧栈；`xt_throw` 在存在帧时长跳到最近 `try`，否则打印 `Uncaught ...` 后退出。无人观察的 promise rejection 同样是致命的：事件循环排空后 `xt_promise_report_unhandled` 会打印 `Unhandled promise rejection` 与原因，并以状态码 1 退出（见[异步](./async.md)）。codegen 还会维护一条影子调用栈（函数名、源文件、行号）：`Error.stack` 用它生成堆栈（见[标准库](./stdlib.md)），原生崩溃时致命信号处理器也会打印同一组帧。当 bundle 的模块体正在执行时，栈会在帧之上打印正在初始化的模块（`    module: <path>`）：所有模块共用一个生成函数，仅看帧只能拿到入口文件的坐标。`--debug` 还会生成 `!DICompileUnit` / `!DISubprogram` / `!DILocation` 元数据，并用 `-g -fno-omit-frame-pointer` 编译，因此 gdb/lldb/WinDbg 可以把指令映射回源码行。
- 回收器从结构体内部追踪的**类型化指针**（promise 的 reaction 列表、正在运行的协程帧、字符串 rope 的子节点）会先在堆表中校验再标记，失效指针被跳过而不是解引用（`XT_GC_VERIFY=1` 会报告）。
- 对象 / 函数：`xt_object` 带原型链，`xt_function` 带属性包（静态成员与 `prototype`）；`xt_new`（实例化）、`xt_instance_of`（原型链）、`xt_object_freeze/is_frozen/from_entries`；`xt_constructors.c` 保存一等公民构造器缓存、构造器原型与静态值分派。
