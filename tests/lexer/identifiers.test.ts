import { describe, expect, it } from "vitest";
import { lex } from "../helpers.js";
import { TokenKind } from "../../src/lexer/token.js";

describe("scanner identifiers", () => {
  it("keeps unicode escapes verbatim in identifier text", () => {
    // `\` is itself an identifier-part character, so the scanner consumes the
    // escape sequence as raw text rather than validating or decoding it.
    const { tokens, diagnostics } = lex("const a\\u0041bc = 1;");
    expect(diagnostics).toHaveLength(0);
    expect(tokens.some((token) => token.kind === TokenKind.Identifier && token.text === "a\\u0041bc")).toBe(true);
  });
});
