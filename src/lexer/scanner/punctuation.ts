/**
 * Punctuation and operator scanning (the operator dispatch table).
 */

import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { Token, TokenKind } from "../token.js";
import { Char, isDigit } from "../char.js";
import type { Scanner } from "../scanner.js";

export interface PunctuationMethods {
  scanPunctuation(this: Scanner, start: number, precededByLineBreak: boolean): Token;
}

export const punctuationMethods: PunctuationMethods = {
  scanPunctuation(this: Scanner, start: number, precededByLineBreak: boolean): Token {
    const text = this.text;
    const ch = text.charCodeAt(this.pos);
    const next = text.charCodeAt(this.pos + 1);
    const third = text.charCodeAt(this.pos + 2);
    const fourth = text.charCodeAt(this.pos + 3);

    const emit = (kind: TokenKind, length: number): Token => {
      this.pos += length;
      return this.makeToken(kind, start, this.pos, text.slice(start, this.pos), precededByLineBreak);
    };

    switch (ch) {
      case Char.OpenBrace:
        return emit(TokenKind.OpenBrace, 1);
      case Char.CloseBrace:
        return emit(TokenKind.CloseBrace, 1);
      case Char.OpenParen:
        return emit(TokenKind.OpenParen, 1);
      case Char.CloseParen:
        return emit(TokenKind.CloseParen, 1);
      case Char.OpenBracket:
        return emit(TokenKind.OpenBracket, 1);
      case Char.CloseBracket:
        return emit(TokenKind.CloseBracket, 1);
      case Char.Semicolon:
        return emit(TokenKind.Semicolon, 1);
      case Char.Comma:
        return emit(TokenKind.Comma, 1);
      case Char.At:
        return emit(TokenKind.At, 1);
      case Char.Tilde:
        return emit(TokenKind.Tilde, 1);
      case Char.Colon:
        return emit(TokenKind.Colon, 1);
      case Char.Dot:
        if (next === Char.Dot && third === Char.Dot) return emit(TokenKind.DotDotDot, 3);
        return emit(TokenKind.Dot, 1);
      case Char.Question:
        if (next === Char.Question) {
          if (third === Char.Equals) return emit(TokenKind.QuestionQuestionEquals, 3);
          return emit(TokenKind.QuestionQuestion, 2);
        }
        if (next === Char.Dot && !isDigit(third)) return emit(TokenKind.QuestionDot, 2);
        return emit(TokenKind.Question, 1);
      case Char.Equals:
        if (next === Char.Equals && third === Char.Equals) return emit(TokenKind.EqualsEqualsEquals, 3);
        if (next === Char.Equals) return emit(TokenKind.EqualsEquals, 2);
        if (next === Char.GreaterThan) return emit(TokenKind.EqualsGreaterThan, 2);
        return emit(TokenKind.Equals, 1);
      case Char.Exclamation:
        if (next === Char.Equals && third === Char.Equals) return emit(TokenKind.ExclamationEqualsEquals, 3);
        if (next === Char.Equals) return emit(TokenKind.ExclamationEquals, 2);
        return emit(TokenKind.Exclamation, 1);
      case Char.Plus:
        if (next === Char.Plus) return emit(TokenKind.PlusPlus, 2);
        if (next === Char.Equals) return emit(TokenKind.PlusEquals, 2);
        return emit(TokenKind.Plus, 1);
      case Char.Minus:
        if (next === Char.Minus) return emit(TokenKind.MinusMinus, 2);
        if (next === Char.Equals) return emit(TokenKind.MinusEquals, 2);
        return emit(TokenKind.Minus, 1);
      case Char.Asterisk:
        if (next === Char.Asterisk) {
          if (third === Char.Equals) return emit(TokenKind.AsteriskAsteriskEquals, 3);
          return emit(TokenKind.AsteriskAsterisk, 2);
        }
        if (next === Char.Equals) return emit(TokenKind.AsteriskEquals, 2);
        return emit(TokenKind.Asterisk, 1);
      case Char.Slash:
        if (next === Char.Equals) return emit(TokenKind.SlashEquals, 2);
        if (this.regexAllowed()) return this.scanRegex(start, precededByLineBreak);
        return emit(TokenKind.Slash, 1);
      case Char.Percent:
        if (next === Char.Equals) return emit(TokenKind.PercentEquals, 2);
        return emit(TokenKind.Percent, 1);
      case Char.LessThan:
        if (next === Char.LessThan) {
          if (third === Char.Equals) return emit(TokenKind.LessThanLessThanEquals, 3);
          return emit(TokenKind.LessThanLessThan, 2);
        }
        if (next === Char.Equals) return emit(TokenKind.LessThanEquals, 2);
        return emit(TokenKind.LessThan, 1);
      case Char.GreaterThan:
        if (next === Char.GreaterThan) {
          if (third === Char.GreaterThan) {
            if (fourth === Char.Equals) return emit(TokenKind.GreaterThanGreaterThanGreaterThanEquals, 4);
            return emit(TokenKind.GreaterThanGreaterThanGreaterThan, 3);
          }
          if (third === Char.Equals) return emit(TokenKind.GreaterThanGreaterThanEquals, 3);
          return emit(TokenKind.GreaterThanGreaterThan, 2);
        }
        if (next === Char.Equals) return emit(TokenKind.GreaterThanEquals, 2);
        return emit(TokenKind.GreaterThan, 1);
      case Char.Ampersand:
        if (next === Char.Ampersand) {
          if (third === Char.Equals) return emit(TokenKind.AmpersandAmpersandEquals, 3);
          return emit(TokenKind.AmpersandAmpersand, 2);
        }
        if (next === Char.Equals) return emit(TokenKind.AmpersandEquals, 2);
        return emit(TokenKind.Ampersand, 1);
      case Char.Bar:
        if (next === Char.Bar) {
          if (third === Char.Equals) return emit(TokenKind.BarBarEquals, 3);
          return emit(TokenKind.BarBar, 2);
        }
        if (next === Char.Equals) return emit(TokenKind.BarEquals, 2);
        return emit(TokenKind.Bar, 1);
      case Char.Caret:
        if (next === Char.Equals) return emit(TokenKind.CaretEquals, 2);
        return emit(TokenKind.Caret, 1);
      default:
        this.pos++;
        this.error(DiagnosticCode.InvalidCharacter, `Invalid character '${text[start]}'`, start, this.pos);
        return this.makeToken(TokenKind.Identifier, start, this.pos, text[start] ?? "", precededByLineBreak);
    }
  },
};
