/**
 * Numeric literal scanning: decimals, radix prefixes, exponents and BigInt.
 */

import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { Token, TokenKind } from "../token.js";
import { Char, isDigit, isHexDigit } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface NumberMethods {
  scanNumber(this: Scanner, start: number, precededByLineBreak: boolean): Token;
  finishNumber(this: Scanner, start: number, precededByLineBreak: boolean, isBigInt: boolean): Token;
}

export const numberMethods: NumberMethods = {
  scanNumber(this: Scanner, start: number, precededByLineBreak: boolean): Token {
    const text = this.text;
    let isBigInt = false;
    // Radix prefixes.
    if (text.charCodeAt(this.pos) === Char.Zero) {
      const next = text.charCodeAt(this.pos + 1);
      if (next === Char.LowerX || next === Char.UpperX) {
        this.pos += 2;
        while (isHexDigit(text.charCodeAt(this.pos)) || text.charCodeAt(this.pos) === Char.Underscore) this.pos++;
        isBigInt = text.charCodeAt(this.pos) === Char.LowerN;
        if (isBigInt) this.pos++;
        return this.finishNumber(start, precededByLineBreak, isBigInt);
      }
      if (next === Char.LowerO || next === Char.UpperO || next === Char.LowerB || next === Char.UpperB) {
        this.pos += 2;
        while (isDigit(text.charCodeAt(this.pos)) || text.charCodeAt(this.pos) === Char.Underscore) this.pos++;
        isBigInt = text.charCodeAt(this.pos) === Char.LowerN;
        if (isBigInt) this.pos++;
        return this.finishNumber(start, precededByLineBreak, isBigInt);
      }
    }

    while (isDigit(text.charCodeAt(this.pos)) || text.charCodeAt(this.pos) === Char.Underscore) this.pos++;
    if (text.charCodeAt(this.pos) === Char.Dot) {
      this.pos++;
      while (isDigit(text.charCodeAt(this.pos)) || text.charCodeAt(this.pos) === Char.Underscore) this.pos++;
    }
    const e = text.charCodeAt(this.pos);
    if (e === Char.LowerE || e === Char.UpperE) {
      const save = this.pos;
      this.pos++;
      const sign = text.charCodeAt(this.pos);
      if (sign === Char.Plus || sign === Char.Minus) this.pos++;
      if (isDigit(text.charCodeAt(this.pos))) {
        while (isDigit(text.charCodeAt(this.pos)) || text.charCodeAt(this.pos) === Char.Underscore) this.pos++;
      } else {
        // Not an exponent, roll back.
        this.pos = save;
      }
    }
    isBigInt = text.charCodeAt(this.pos) === Char.LowerN;
    if (isBigInt) this.pos++;
    return this.finishNumber(start, precededByLineBreak, isBigInt);
  },

  finishNumber(this: Scanner, start: number, precededByLineBreak: boolean, isBigInt: boolean): Token {
    let raw = this.text.slice(start, this.pos);
    const cleaned = raw.replace(/_/g, "");
    if (isBigInt) {
      raw = raw.slice(0, -1).replace(/_/g, "");
      const parsed = Number(raw);
      if (parsed !== parsed) {
        this.error(DiagnosticCode.InvalidNumber, `Invalid BigInt literal '${raw}'`, start, this.pos);
        return this.makeToken(TokenKind.BigIntLiteral, start, this.pos, raw, precededByLineBreak, 0);
      }
      return this.makeToken(TokenKind.BigIntLiteral, start, this.pos, raw, precededByLineBreak, parsed);
    }
    let value: number;
    if (/^0[xX]/.test(cleaned)) value = parseInt(cleaned.slice(2), 16);
    else if (/^0[oO]/.test(cleaned)) value = parseInt(cleaned.slice(2), 8);
    else if (/^0[bB]/.test(cleaned)) value = parseInt(cleaned.slice(2), 2);
    else value = Number(cleaned);
    if (Number.isNaN(value)) {
      this.error(DiagnosticCode.InvalidNumber, `Invalid numeric literal '${raw}'`, start, this.pos);
      value = 0;
    }
    return this.makeToken(TokenKind.NumericLiteral, start, this.pos, raw, precededByLineBreak, value);
  },
};
