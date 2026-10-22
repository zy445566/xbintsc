# Statements

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/statements.md)

## Parsed but codegen reports `UnsupportedFeature`

| Syntax | Status | Notes |
| --- | --- | --- |
| `namespace` / `module` declarations | parse ✓, codegen ✗ | → "does not yet support this statement (module declaration)" |

> Labeled statements (`label: statement`, `break label`, `continue label`) are
> implemented, including labels on non-loop statements.

## Not supported by the parser (immediate syntax error)

None known at the statement level. The parser is intentionally permissive and
accepts most TypeScript statement syntax; unsupported forms are rejected later
by code generation (see above).
