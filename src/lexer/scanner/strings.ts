/**
 * String and template literal scanning, including escape sequences and the
 * template continuation protocol used by the parser.
 */

import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { Token, TokenKind } from "../token.js";
import { Char, isDigit, isLineBreak } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface StringMethods {
  scanString(this: Scanner, start: number, precededByLineBreak: boolean, quote: number): Token;
  scanEscapeSequence(this: Scanner): string;
  scanTemplate(this: Scanner, start: number, precededByLineBreak: boolean, isHead: boolean): Token;
  continueTemplate(this: Scanner, offset: number): Token;
}

export const stringMethods: StringMethods = {
  scanString(this: Scanner, start: number, precededByLineBreak: boolean, quote: number): Token {
    this.pos++;
    let result = "";
    while (this.pos < this.text.length) {
      const ch = this.text.charCodeAt(this.pos);
      if (ch === quote) {
        this.pos++;
        return this.makeToken(TokenKind.StringLiteral, start, this.pos, this.text.slice(start, this.pos), precededByLineBreak, result);
      }
      if (ch === Char.Backslash) {
        result += this.scanEscapeSequence();
        continue;
      }
      if (isLineBreak(ch)) {
        this.error(DiagnosticCode.UnterminatedString, "Unterminated string literal", start);
        break;
      }
      result += this.text[this.pos];
      this.pos++;
    }
    this.error(DiagnosticCode.UnterminatedString, "Unterminated string literal", start);
    return this.makeToken(TokenKind.StringLiteral, start, this.pos, this.text.slice(start, this.pos), precededByLineBreak, result);
  },

  scanEscapeSequence(this: Scanner): string {
    this.pos++; // backslash
    const ch = this.text.charCodeAt(this.pos);
    switch (ch) {
      case Char.LowerN:
        this.pos++;
        return "\n";
      case Char.LowerT:
        this.pos++;
        return "\t";
      case Char.LowerR:
        this.pos++;
        return "\r";
      case Char.LowerB:
        this.pos++;
        return "\b";
      case Char.LowerF:
        this.pos++;
        return "\f";
      case Char.LowerU:
        this.pos++;
        if (this.text.charCodeAt(this.pos) === Char.OpenBrace) {
          const close = this.text.indexOf("}", this.pos);
          const hex = this.text.slice(this.pos + 1, close);
          this.pos = close + 1;
          return String.fromCodePoint(parseInt(hex, 16) || 0);
        } else {
          const hex = this.text.slice(this.pos, this.pos + 4);
          this.pos += 4;
          return String.fromCharCode(parseInt(hex, 16) || 0);
        }
      case Char.LowerX: {
        const hex = this.text.slice(this.pos + 1, this.pos + 3);
        this.pos += 3;
        return String.fromCharCode(parseInt(hex, 16) || 0);
      }
      case Char.Zero:
        // \0 is only a null escape when not followed by a digit.
        if (!isDigit(this.text.charCodeAt(this.pos + 1))) {
          this.pos++;
          return "\0";
        }
        this.pos++;
        return "\0";
      default:
        this.pos++;
        return this.text[this.pos - 1] ?? "";
    }
  },

  /** Scans a template literal chunk starting at a backtick or after `}`. */
  scanTemplate(this: Scanner, start: number, precededByLineBreak: boolean, isHead: boolean): Token {
    if (isHead) this.pos++; // opening backtick
    let result = "";
    for (;;) {
      if (this.pos >= this.text.length) {
        this.error(DiagnosticCode.UnterminatedTemplate, "Unterminated template literal", start);
        return this.makeToken(TokenKind.NoSubstitutionTemplateLiteral, start, this.pos, this.text.slice(start, this.pos), precededByLineBreak, result);
      }
      const ch = this.text.charCodeAt(this.pos);
      if (ch === Char.Backtick) {
        this.pos++;
        return this.makeToken(TokenKind.NoSubstitutionTemplateLiteral, start, this.pos, this.text.slice(start, this.pos), precededByLineBreak, result);
      }
      if (ch === Char.Dollar && this.text.charCodeAt(this.pos + 1) === Char.OpenBrace) {
        this.pos += 2;
        return this.makeToken(TokenKind.TemplateHead, start, this.pos, this.text.slice(start, this.pos), precededByLineBreak, result);
      }
      if (ch === Char.Backslash) {
        result += this.scanEscapeSequence();
        continue;
      }
      result += this.text[this.pos];
      this.pos++;
    }
  },

  /** Called by the parser after a template substitution expression. */
  continueTemplate(this: Scanner, offset: number): Token {
    // `offset` points at the `}` that closes the substitution; skip it so the
    // following characters are scanned as template text.
    this.pos = Math.max(0, Math.min(offset + 1, this.text.length));
    this._token = this.scanTemplate(this.pos, false, /* isHead */ false);
    return this._token;
  },
};
