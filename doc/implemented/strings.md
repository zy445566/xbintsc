# Strings and text

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/strings.md)

Location: the string value lives in the core runtime (`runtime/xt_values.c`);
the method surface is dispatched from `runtime/xt_stdlib.c` / `xt_stdlib2.c`;
`TextEncoder` / `TextDecoder` live in `runtime/xt_text_codec.c`.

## Value model

- UTF-8 storage exposed through JavaScript's UTF-16 view: `length`, indexing, `slice`, `indexOf`, `split`, `padStart` and `codePointAt` count code units; `for...of` and `Array.from` count code points; lone surrogate halves are kept as WTF-8 and recombined on concatenation.
- Lazy cons-node concatenation: bytes are flattened on first observation, so append loops stay linear.
- Equality comparison, and formatted number-to-string.
- `xt_string_length` reports bytes for I/O; `xt_string_utf16_length` reports the JavaScript length.

## Methods

- `charAt` `charCodeAt` `codePointAt` `indexOf` `lastIndexOf` `includes` `startsWith` `endsWith` `slice` `substring` `substr` `split` `match` `replace` `replaceAll` `search` `toUpperCase` `toLowerCase` `trim` `trimStart` `trimEnd` `padStart` `padEnd` `repeat` `concat` `at` `localeCompare` `valueOf`
- `String.match` / `lastIndexOf` / `split` with a limit; RegExp capture groups flow through `match` / `replace` / `split` / `search` (see [Standard library](./stdlib.md)).
- `String` statics: `fromCharCode` / `fromCodePoint` / `raw`; tagged templates keep their raw strings.

## Text encoding

- `TextEncoder` / `TextDecoder` encode and decode through a `Uint8Array`; only the UTF-8 encoding is supported.

Known divergences (`String.prototype.normalize`, byte-oriented RegExp matching)
are listed in [Unimplemented: standard library](../unimplemented/stdlib.md).
