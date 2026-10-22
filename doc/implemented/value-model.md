# Value model and calling convention

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/value-model.md)

Location: `src/codegen/values.ts`, `runtime/rt.h`

- All JS values are unified as a 64-bit `xt_value` (NaN-boxing).
- Doubles are not boxed; other types are tagged pointers with a 16-bit tag in the high bits and a 48-bit payload.
- Tags: `undefined` / `null` / `false` / `true` / `string` / `object` / `array` / `function`.
- Uniform function ABI:

```c
xt_value fn(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);
```

- `thisValue` is threaded as the first ABI parameter (exception-safe, supports nesting); arrow functions inherit `this` lexically from an extra environment slot.
- Closures thread captured variables through `env` (by reference, boxed); direct calls and closure calls share one code path.
- `xt_object` carries a prototype field, and property lookup walks the prototype chain; `xt_function` carries a `prototype` and a property bag (static members). `xt_array` also carries a `[[Prototype]]` (Array.prototype). `Object.prototype` is the root of ordinary objects and provides `hasOwnProperty` / `toString` / `valueOf` / `isPrototypeOf` / `propertyIsEnumerable` / `__proto__`; `Object.getPrototypeOf({}) === Object.prototype`.
