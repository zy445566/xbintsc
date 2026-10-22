# 类与面向对象

> 属于[未实现语法与功能](../unimplemented.md) · [English](../../unimplemented/classes.md)

| 特性 | 现状 |
| --- | --- |
| `class` 声明 / 类表达式 | ✓ 已实现 |
| 构造函数 `constructor` | ✓ |
| 实例字段 / 属性声明 | ✓ |
| 方法 | ✓ |
| `static` 字段 / 方法 | ✓ |
| 继承 `extends` / `super` | ✓（单级正确；`super` 取 `this` 原型的原型，理论缺陷见[运行时偏差](./deviations.md)） |
| 原型链 / 方法查找 | ✓ |
| `instanceof` | ✓ |
| `new` / 实例化 | ✓ |
| `get` / `set` 访问器 | ✓ 已实现 |
| 参数属性 `constructor(public x: T)` | ✓ 已实现 |
| 私有字段 `#x` | ✓ 已实现（以字面量 `#x` 作为键存储；无访问控制强制） |
| `enum` / `const enum` | ✓ 已实现（正向 + 反向映射） |
| `private` / `protected` / `public` / `readonly` 修饰符 | ✗ 无访问控制（擦除） |
| `abstract` / `implements` | ✗（擦除） |
| 父子类 `#x` 同名 | ✗ 可能混用（同一字面量键） |
