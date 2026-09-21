/**
 * Regular expression literal scanning and the disambiguation rule that decides
 * whether a `/` is a regex or a division operator.
 */

import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { Token, TokenKind } from "../token.js";
import { Char, isIdentifierPart } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface RegexMethods {
  regexAllowed(this: Scanner): boolean;
  scanRegex(this: Scanner, start: number, precededByLineBreak: boolean): Token;
}

export const regexMethods: RegexMethods = {
  /**
   * True when a `/` in this position starts a regular expression rather than a
   * division. The decision is made from the previous significant token.
   */
  regexAllowed(this: Scanner): boolean {
    switch (this._token.kind) {
      case TokenKind.Identifier:
      case TokenKind.PrivateIdentifier:
      case TokenKind.NumericLiteral:
      case TokenKind.BigIntLiteral:
      case TokenKind.StringLiteral:
      case TokenKind.NoSubstitutionTemplateLiteral:
      case TokenKind.TemplateTail:
      case TokenKind.RegularExpressionLiteral:
      case TokenKind.CloseParen:
      case TokenKind.CloseBracket:
      case TokenKind.TrueKeyword:
      case TokenKind.FalseKeyword:
      case TokenKind.NullKeyword:
      case TokenKind.UndefinedKeyword:
      case TokenKind.ThisKeyword:
      case TokenKind.PlusPlus:
      case TokenKind.MinusMinus:
        return false;
      default:
        return true;
    }
  },

  /** Scan a `/pattern/flags` literal, starting at the leading slash. */
  scanRegex(this: Scanner, start: number, precededByLineBreak: boolean): Token {
    this.pos++; // opening slash
    let inClass = false;
    for (;;) {
      if (this.pos >= this.text.length) {
        this.error(DiagnosticCode.InvalidCharacter, "Unterminated regular expression", start);
        break;
      }
      const ch = this.text.charCodeAt(this.pos);
      if (ch === Char.Backslash) {
        this.pos += 2;
        continue;
      }
      if (ch === Char.LineFeed || ch === Char.CarriageReturn) break;
      if (ch === 91 /* [ */) inClass = true;
      else if (ch === 93 /* ] */) inClass = false;
      else if (ch === Char.Slash && !inClass) {
        this.pos++;
        break;
      }
      this.pos++;
    }
    // Flags.
    while (this.pos < this.text.length && isIdentifierPart(this.text.charCodeAt(this.pos))) this.pos++;
    return this.makeToken(
      TokenKind.RegularExpressionLiteral,
      start,
      this.pos,
      this.text.slice(start, this.pos),
      precededByLineBreak,
    );
  },
};
