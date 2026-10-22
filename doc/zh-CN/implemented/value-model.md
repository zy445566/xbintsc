# 值模型与调用约定

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/value-model.md)

实现位置：`src/codegen/values.ts`、`runtime/rt.h`

- 所有 JS 值统一为 64 位 `xt_value`（NaN-boxing）。
- 双精度浮点不装箱；其它类型为高 16 位 tag + 48 位 payload 的带标签指针。
- Tag：`undefined` / `null` / `false` / `true` / `string` / `object` / `array` / `function`。
- 统一函数 ABI：

```c
xt_value fn(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
```

- `thisValue` 作为首个 ABI 参数线程化（异常安全、支持嵌套）；箭头函数从环境额外槽位词法继承 `this`。
- 闭包通过 `env` 线程化捕获变量（引用传递，box 包装），直接调用与闭包调用共用同一代码路径。
- `xt_object` 带原型字段，属性查找沿原型链；`xt_function` 带 `prototype` 与属性包（静态成员）。`xt_array` 也带 `[[Prototype]]`（Array.prototype）。`Object.prototype` 是普通对象的原型根，提供 `hasOwnProperty` / `toString` / `valueOf` / `isPrototypeOf` / `propertyIsEnumerable` / `__proto__`；`Object.getPrototypeOf({}) === Object.prototype`。
