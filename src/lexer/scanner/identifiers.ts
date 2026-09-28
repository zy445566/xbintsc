/**
 * Identifier scanning (identifiers, keywords and private `#name`s).
 */

import { KEYWORDS, Token, TokenKind } from "../token.js";
import { isIdentifierPart } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface IdentifierMethods {
  scanIdentifier(this: Scanner, start: number, precededByLineBreak: boolean): Token;
}

export const identifierMethods: IdentifierMethods = {
  scanIdentifier(this: Scanner, start: number, precededByLineBreak: boolean): Token {
    this.pos++;
    /* `\` is itself an identifier part, so unicode escapes such as `a\u0041b`
     * are kept verbatim in the identifier text (see the lexer tests). */
    while (this.pos < this.text.length) {
      if (isIdentifierPart(this.text.charCodeAt(this.pos))) {
        this.pos++;
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
