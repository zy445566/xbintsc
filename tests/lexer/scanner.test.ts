import { describe, expect, it } from "vitest";
import { lex } from "../helpers.js";
import { TokenKind } from "../../src/lexer/token.js";

function kinds(text: string): string[] {
  return lex(text).tokens.map((token) => token.kind);
}

function texts(text: string): string[] {
  return lex(text).tokens.map((token) => token.text);
}

describe("scanner", () => {
  it("always terminates with an EOF token", () => {
    const { tokens } = lex("");
    expect(tokens).toHaveLength(1);
    expect(tokens[0]!.kind).toBe(TokenKind.EndOfFile);
  });

  it("distinguishes keywords from identifiers", () => {
    expect(kinds("const x = 1")).toEqual([
      TokenKind.ConstKeyword,
      TokenKind.Identifier,
      TokenKind.Equals,
      TokenKind.NumericLiteral,
      TokenKind.EndOfFile,
    ]);
    expect(kinds("constant")).toEqual([TokenKind.Identifier, TokenKind.EndOfFile]);
  });

  it("scans every numeric literal form", () => {
    const { tokens, diagnostics } = lex("0 42 3.14 .5 1e3 1E-3 0xFF 0b1010 0o17 1_000");
    expect(diagnostics).toHaveLength(0);
    const literals = tokens.filter((t) => t.kind === TokenKind.NumericLiteral).map((t) => t.text);
    expect(literals).toEqual(["0", "42", "3.14", ".5", "1e3", "1E-3", "0xFF", "0b1010", "0o17", "1_000"]);
  });

  it("scans strings with escapes", () => {
    const { tokens, diagnostics } = lex('"a\\n\\t\\"b" \'c\'');
    expect(diagnostics).toHaveLength(0);
    const literals = tokens.filter((t) => t.kind === TokenKind.StringLiteral);
    expect(literals).toHaveLength(2);
    expect(literals[0]!.text).toBe('"a\\n\\t\\"b"');
  });

  it("reports unterminated strings", () => {
    const { diagnostics } = lex('"oops');
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });

  it("scans template literal heads and template text", () => {
    const { tokens } = lex("`a${");
    expect(tokens[0]!.kind).toBe(TokenKind.TemplateHead);
    expect(tokens[0]!.text).toBe("`a${");
  });

  it("scans a no-substitution template as a single token", () => {
    const { tokens } = lex("`plain`");
    expect(tokens[0]!.kind).toBe(TokenKind.NoSubstitutionTemplateLiteral);
    expect(tokens[0]!.text).toBe("`plain`");
  });

  it("scans operators greedily", () => {
    expect(kinds("a === b !== c ?? d => e")).toEqual([
      TokenKind.Identifier,
      TokenKind.EqualsEqualsEquals,
      TokenKind.Identifier,
      TokenKind.ExclamationEqualsEquals,
      TokenKind.Identifier,
      TokenKind.QuestionQuestion,
      TokenKind.Identifier,
      TokenKind.EqualsGreaterThan,
      TokenKind.Identifier,
      TokenKind.EndOfFile,
    ]);
  });

  it("distinguishes regex literals from division", () => {
    const regex = lex("const r = /ab+c/gi;");
    const regexToken = regex.tokens.find((t) => t.kind === TokenKind.RegularExpressionLiteral);
    expect(regexToken?.text).toBe("/ab+c/gi");
    expect(regexToken?.kind).toBe(TokenKind.RegularExpressionLiteral);

    const division = lex("const q = a / b / c;");
    expect(division.tokens.filter((t) => t.kind === TokenKind.Slash)).toHaveLength(2);
  });

  it("skips line and block comments", () => {
    const { tokens, diagnostics } = lex("// line\n/* block */ x");
    expect(diagnostics).toHaveLength(0);
    expect(tokens[0]!.kind).toBe(TokenKind.Identifier);
  });

  it("tracks whether a token is preceded by a line break", () => {
    const { tokens } = lex("a\nb c");
    const identifiers = tokens.filter((t) => t.kind === TokenKind.Identifier);
    expect(identifiers[0]!.precededByLineBreak).toBe(false);
    expect(identifiers[1]!.precededByLineBreak).toBe(true);
    expect(identifiers[2]!.precededByLineBreak).toBe(false);
  });

  it("normalizes BOM and CRLF through SourceFile", () => {
    expect(texts("\uFEFFa\r\nb")).toEqual(["a", "b", ""]);
  });

  it("reports unterminated block comments", () => {
    const { diagnostics } = lex("/* never ends");
    expect(diagnostics.map((d) => d.code)).toContain(1003);
  });
});
