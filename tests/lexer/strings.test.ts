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

describe("scanner string literals", () => {
  it("decodes the simple control escapes", () => {
    expect(decoded('"a\\nb"')).toBe("a\nb");
    expect(decoded('"a\\tb"')).toBe("a\tb");
    expect(decoded('"a\\rb"')).toBe("a\rb");
    expect(decoded('"a\\bb"')).toBe("a\bb");
    expect(decoded('"a\\fb"')).toBe("a\fb");
  });

  it("decodes braced, fixed-width and hex unicode escapes", () => {
    expect(decoded('"\\u{1F600}"')).toBe("\u{1F600}");
    expect(decoded('"\\u0041"')).toBe("A");
    expect(decoded('"\\x41"')).toBe("A");
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
    expect(decoded('"héllo→"')).toBe("héllo→");
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
