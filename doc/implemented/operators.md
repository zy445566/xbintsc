# Operators and assignment

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/operators.md)

Location: `src/codegen/llvm.ts` (`BINARY_RUNTIME`, `emitPrefix`, `emitPostfix`, `emitAssignment`)

## Arithmetic

`+ - * / % **` (`**` is right-associative)

## Comparison and equality

`< <= > >=`, `== !=` (loose equality), `=== !==` (strict equality)

## Logical and short-circuit

`&& || ??` (with short-circuit evaluation), `!`

## Bitwise

`& | ^ ~ << >> >>>`

## Unary

`+ - ! ~`, prefix / postfix `++ --`

## Assignment

`= += -= *= /= %= **= <<= >>= >>>= &= |= ^= &&= ||= ??=`

## Other expression operators

- Comma expression `,`
- `in` operator (`key in obj`, mapped to `xt_in`)
- `delete obj.key` / `delete obj[key]` (mapped to `xt_delete`)
- `instanceof` (mapped to `xt_instance_of`, walking the prototype chain)

> `typeof` / `void` are implemented as unary operators.
