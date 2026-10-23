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

  it("scans a Chinese identifier as one identifier token", () => {
    const { tokens, diagnostics } = lex("const 姓名 = 1;");
    expect(diagnostics).toHaveLength(0);
    const identifiers = tokens.filter((token) => token.kind === TokenKind.Identifier);
    expect(identifiers).toHaveLength(1);
    const name = Buffer.from("姓名", "utf8").toString("latin1");
    expect(identifiers[0]!.text).toBe(name);
    // The scanner walks UTF-8 source bytes, so the token covers all six bytes
    // of the name rather than stopping after the first one.
    expect(identifiers[0]!.start).toBe("const ".length);
    expect(identifiers[0]!.end).toBe(identifiers[0]!.start + name.length);
  });
});
