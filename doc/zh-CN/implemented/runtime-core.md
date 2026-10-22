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

- 分配器：`xt_alloc` 背后的非移动标记-清扫回收器（显式根、子系统根提供者与保守 C 栈扫描）。
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
- 异常：`xt_try_enter` / `xt_try_exception` / `xt_try_leave` 维护 `_setjmp` 帧栈；`xt_throw` 在存在帧时长跳到最近 `try`，否则打印 `Uncaught ...` 后退出。codegen 还会维护一条影子调用栈（函数名、源文件、行号）：`Error.stack` 用它生成堆栈（见[标准库](./stdlib.md)），原生崩溃时致命信号处理器也会打印同一组帧。`--debug` 还会生成 `!DICompileUnit` / `!DISubprogram` / `!DILocation` 元数据，并用 `-g -fno-omit-frame-pointer` 编译，因此 gdb/lldb/WinDbg 可以把指令映射回源码行。
- 对象 / 函数：`xt_object` 带原型链，`xt_function` 带属性包（静态成员与 `prototype`）；`xt_new`（实例化）、`xt_instance_of`（原型链）、`xt_object_freeze/is_frozen/from_entries`；`xt_constructors.c` 保存一等公民构造器缓存、构造器原型与静态值分派。
