import { describe, expect, it } from "vitest";
import { lex } from "../helpers.js";
import { TokenKind } from "../../src/lexer/token.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

describe("numeric literal edge cases", () => {
  it("rolls back an incomplete exponent and re-scans it as an identifier", () => {
    const { tokens, diagnostics } = lex("1e");
    expect(diagnostics).toHaveLength(0);
    expect(tokens[0]).toMatchObject({ kind: TokenKind.NumericLiteral, text: "1", value: 1 });
    expect(tokens[1]!.kind).toBe(TokenKind.Identifier);
  });

  it("reports an invalid BigInt literal", () => {
    const { tokens, diagnostics } = lex("0b2n");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.InvalidNumber)).toBe(true);
    expect(tokens[0]!.kind).toBe(TokenKind.BigIntLiteral);
  });

  it("reports an invalid numeric literal and recovers with value 0", () => {
    const { tokens, diagnostics } = lex("0b2");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.InvalidNumber)).toBe(true);
    expect(tokens[0]).toMatchObject({ kind: TokenKind.NumericLiteral, value: 0 });
  });
});
