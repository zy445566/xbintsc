/**
 * Identifier scanning (identifiers, keywords and private `#name`s).
 */

import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { KEYWORDS, Token, TokenKind } from "../token.js";
import { Char, isIdentifierPart } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface IdentifierMethods {
  scanIdentifier(this: Scanner, start: number, precededByLineBreak: boolean): Token;
}

export const identifierMethods: IdentifierMethods = {
  scanIdentifier(this: Scanner, start: number, precededByLineBreak: boolean): Token {
    this.pos++;
    while (this.pos < this.text.length) {
      const ch = this.text.charCodeAt(this.pos);
      if (isIdentifierPart(ch)) {
        this.pos++;
      } else if (ch === Char.Backslash && this.text.charCodeAt(this.pos + 1) === Char.LowerU) {
        this.pos += 2;
        const hex = this.text.slice(this.pos, this.pos + 4);
        this.pos += 4;
        if (!/^[0-9a-fA-F]{4}$/.test(hex)) {
          this.error(DiagnosticCode.InvalidEscape, "Invalid unicode escape in identifier", this.pos - 4, this.pos);
        }
      } else {
        break;
      }
    }
    const text = this.text.slice(start, this.pos);
    const keyword = KEYWORDS.get(text);
    const kind = keyword ?? TokenKind.Identifier;
    return this.makeToken(kind, start, this.pos, text, precededByLineBreak);
  },
};
