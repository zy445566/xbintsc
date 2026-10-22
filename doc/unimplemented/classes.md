# Classes and object orientation

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/classes.md)

| Feature | Status |
| --- | --- |
| `class` declarations / class expressions | ✓ implemented |
| `constructor` | ✓ |
| Instance fields / property declarations | ✓ |
| Methods | ✓ |
| `static` fields / methods | ✓ |
| Inheritance `extends` / `super` | ✓ (single level correct; `super` takes the prototype of `this`'s prototype — see [Runtime deviations](./deviations.md)) |
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
