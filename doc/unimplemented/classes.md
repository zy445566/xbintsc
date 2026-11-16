# Classes and object orientation

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/classes.md)

| Feature | Status |
| --- | --- |
| `class` declarations / class expressions | ✓ implemented |
| `constructor` | ✓ |
| Instance fields / property declarations | ✓ |
| Methods | ✓ |
| `static` fields / methods | ✓ |
| `static { ... }` blocks | ✓ implemented (runs once at class definition; static fields and blocks run in declaration order, static methods are installed first; `this` is the class, `super.x` reads the parent constructor) |
| `static get` / `static set` accessors | ✓ (accessors on the constructor value; inherited static setters are honoured) |
| `super[key]` / `super[key](...)` | ✓ |
| Inheritance `extends` / `super` | ✓ (single level correct; `super` takes the prototype of `this`'s prototype — see [Runtime deviations](./deviations.md)) |
| `extends` a native/extension class (`class X extends EventEmitter`) | ✓ (the parent is never constructed at class-definition time: a class value publishes its prototype, an extension constructor value publishes its own — `EventEmitter.prototype` — through the extension registry, and anything else gets the plain object a function's `.prototype` is. `super(...)` runs the parent's `__ctor` hook, or the parent constructor itself when it has none) |
| Prototype chain / method lookup | ✓ |
| `instanceof` | ✓ |
| `new` / instantiation | ✓ |
| `get` / `set` accessors | ✓ implemented |
| Parameter properties `constructor(public x: T)` | ✓ implemented |
| Private fields `#x` | ✓ implemented (stored under a literal `#x` key; no accessibility enforcement) |
| `enum` / `const enum` | ✓ implemented (forward + reverse mapping) |
| `private` / `protected` / `public` / `readonly` modifiers | ✗ no access control (erased) |
| `abstract` / `implements` | ✗ (erased) |
| Parent/child `#x` name collisions | ✗ may alias (same literal key) |
