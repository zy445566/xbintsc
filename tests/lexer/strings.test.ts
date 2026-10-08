import { describe, expect, it } from "vitest";
import { lex } from "../helpers.js";
import { TokenKind } from "../../src/lexer/token.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
function decoded(source: string): any {
  const { tokens, diagnostics } = lex(source);
  expect(diagnostics).toHaveLength(0);
  return tokens[0]!.value;
}

/**
 * The text a code point's escape decodes to: its UTF-8 bytes, one code unit
 * each (`escapeText` in the scanner spells them the same way, so both hosts
 * store a literal the same).
 */
function escaped(code: number): string {
  return Buffer.from(String.fromCodePoint(code), "utf8").toString("latin1");
}

describe("scanner string literals", () => {
  it("decodes the simple control escapes", () => {
    expect(decoded('"a\\nb"')).toBe("a\nb");
    expect(decoded('"a\\tb"')).toBe("a\tb");
    expect(decoded('"a\\rb"')).toBe("a\rb");
    expect(decoded('"a\\bb"')).toBe("a\bb");
    expect(decoded('"a\\fb"')).toBe("a\fb");
  });

  it("decodes braced, fixed-width and hex unicode escapes", () => {
    // An escape names a code point; the literal holds that code point's UTF-8
    // bytes, which codegen emits unchanged.
    expect(decoded('"\\u{1F600}"')).toBe(escaped(0x1f600));
    expect(decoded('"\\u0041"')).toBe("A");
    expect(decoded('"\\u00e9"')).toBe(escaped(0xe9));
    expect(decoded('"\\x41"')).toBe("A");
    expect(decoded('"\\xe9"')).toBe(escaped(0xe9));
  });

  it("falls back to NUL for malformed escapes", () => {
    expect(decoded('"\\u{zz}"')).toBe("\0");
    expect(decoded('"\\xZZ"')).toBe("\0");
  });

  it("handles the null escape with and without a following digit", () => {
    expect(decoded('"\\0"')).toBe("\0");
    // `\0` followed by a digit is not an octal escape: NUL then the digit.
    expect(decoded('"\\01"')).toBe("\u00001");
  });

  it("treats unknown escapes as the escaped character", () => {
    expect(decoded('"\\q"')).toBe("q");
  });

  it("keeps surrogate pairs and non-ASCII characters intact", () => {
    expect(decoded('"héllo→"')).toBe(Buffer.from("héllo→", "utf8").toString("latin1"));
  });

  it("reports an unterminated string when a line break is reached", () => {
    const { diagnostics } = lex('"line\nnext"');
    expect(diagnostics.map((d) => d.code)).toContain(DiagnosticCode.UnterminatedString);
    expect(diagnostics.every((d) => d.category === "error")).toBe(true);
  });

  it("reports an unterminated string at end of input", () => {
    const { diagnostics } = lex('"never closed');
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnterminatedString)).toBe(true);
  });
});

describe("scanner template literals", () => {
  it("decodes escapes in a no-substitution template", () => {
    const { tokens, diagnostics } = lex("`a\\nb`");
    expect(diagnostics).toHaveLength(0);
    expect(tokens[0]!.kind).toBe(TokenKind.NoSubstitutionTemplateLiteral);
    expect(tokens[0]!.value).toBe("a\nb");
  });

  it("keeps multi-line template text", () => {
    const { tokens } = lex("`line1\nline2`");
    expect(tokens[0]!.kind).toBe(TokenKind.NoSubstitutionTemplateLiteral);
    expect(tokens[0]!.value).toBe("line1\nline2");
  });

  it("decodes escapes in a template head", () => {
    const { tokens } = lex("`a\\t${x}`");
    expect(tokens[0]!.kind).toBe(TokenKind.TemplateHead);
    expect(tokens[0]!.value).toBe("a\t");
  });

  it("reports an unterminated template literal", () => {
    const { tokens, diagnostics } = lex("`never closed");
    expect(tokens[0]!.kind).toBe(TokenKind.NoSubstitutionTemplateLiteral);
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnterminatedTemplate)).toBe(true);
  });
});
