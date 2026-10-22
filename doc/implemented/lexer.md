# Lexer

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/lexer.md)

Location: `src/lexer/scanner.ts`, `src/lexer/token.ts`

- Full token classification: identifiers, keywords, private identifiers, numbers, strings, templates, regex (scanning), all punctuation and operators.
- Full keyword table: `abstract any as asserts async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object package private protected public readonly return satisfies set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield`
- Numeric literals:
  - Decimal, `0x` hex, `0o` octal, `0b` binary
  - Underscore separators `1_000_000`
  - Fractions, exponents `1.5e3`
  - BigInt literals (`10n`, `0xFFn`) evaluate to arbitrary-precision integers (arithmetic, bitwise, shifts, comparisons, `toString(radix)`, `BigInt()` / `BigInt.asIntN` / `BigInt.asUintN`)
  - BigInt literal values are parsed exactly into `bigint` (never routed through `Number`), so the token / AST value of a literal beyond 2^53 keeps full precision
- String literals:
  - Single / double quotes
  - Escapes: `\n \t \r \b \f \0 \\ \' \"`, `\xHH`, `\uHHHH`, `\u{...}`
- Template literals (lexer level): no-substitution templates, `TemplateHead`, `TemplateMiddle`, `TemplateTail`, with `${}` and escapes.
- Regex literal scanning (`/pattern/flags`), distinguishing division `/`.
- Private identifiers `#name`.
- Comments: line `//` and block `/* */` (with unterminated diagnostics).
- Newline and whitespace tracking (for ASI), BOM / CRLF normalization (`SourceFile`).
- Lexical diagnostics: unterminated string / template / comment, illegal character, illegal number, illegal escape.
