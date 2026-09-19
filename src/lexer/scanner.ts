import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import type { SourceFile } from "../diagnostics/source.js";
import { KEYWORDS, Token, TokenKind } from "./token.js";
import {
  Char,
  isDigit,
  isHexDigit,
  isIdentifierPart,
  isIdentifierStart,
  isLineBreak,
  isWhiteSpace,
} from "./char.js";

/**
 * Hand-written scanner for the TypeScript language. It produces a flat token
 * stream consumed by the parser. Comments and trivia are skipped, but line
 * breaks are tracked so the parser can implement ASI (automatic semicolon
 * insertion) correctly.
 */
export class Scanner {
  private readonly text: string;
  private pos = 0;
  private readonly diagnostics: DiagnosticBag;
  private readonly source: SourceFile;
  private _token: Token = {
    kind: TokenKind.EndOfFile,
    start: 0,
    end: 0,
    text: "",
    precededByLineBreak: false,
  };

  constructor(source: SourceFile, diagnostics: DiagnosticBag) {
    this.source = source;
    this.text = source.text;
    this.diagnostics = diagnostics;
  }

  get token(): Token {
    return this._token;
  }

  get position(): number {
    return this.pos;
  }

  /**
   * Reset the scanner read head. Used by the parser to re-scan the `}` that
   * terminates a template substitution as template text instead of a brace.
   */
  seek(offset: number): void {
    this.pos = Math.max(0, Math.min(offset, this.text.length));
  }

  /** Advance to the next token. */
  nextToken(): Token {
    this._token = this.scan();
    return this._token;
  }

  /** Scan everything, useful for lexer tests. */
  tokenize(): Token[] {
    const tokens: Token[] = [];
    for (;;) {
      const token = this.nextToken();
      tokens.push(token);
      if (token.kind === TokenKind.EndOfFile) break;
    }
    return tokens;
  }

  private error(code: DiagnosticCode, message: string, start: number, end = this.pos): void {
    this.diagnostics.error(code, message, { start, end }, this.source.fileName);
  }

  private scan(): Token {
    let precededByLineBreak = false;
    // Skip trivia.
    for (;;) {
      const ch = this.text.charCodeAt(this.pos);
      if (Number.isNaN(ch)) break; // EOF
      if (ch === Char.LineFeed || ch === Char.CarriageReturn) {
        precededByLineBreak = true;
        this.pos++;
        continue;
      }
      if (isLineBreak(ch)) {
        precededByLineBreak = true;
        this.pos++;
        continue;
      }
      if (isWhiteSpace(ch)) {
        this.pos++;
        continue;
      }
      if (ch === Char.Slash && this.text.charCodeAt(this.pos + 1) === Char.Slash) {
        this.pos += 2;
        while (this.pos < this.text.length && !isLineBreak(this.text.charCodeAt(this.pos))) this.pos++;
        continue;
      }
      if (ch === Char.Slash && this.text.charCodeAt(this.pos + 1) === Char.Asterisk) {
        const start = this.pos;
        this.pos += 2;
        let closed = false;
        while (this.pos < this.text.length) {
          if (this.text.charCodeAt(this.pos) === Char.Asterisk && this.text.charCodeAt(this.pos + 1) === Char.Slash) {
            this.pos += 2;
            closed = true;
            break;
          }
          if (isLineBreak(this.text.charCodeAt(this.pos))) precededByLineBreak = true;
          this.pos++;
        }
        if (!closed) this.error(DiagnosticCode.UnterminatedComment, "Unterminated block comment", start);
        continue;
      }
      break;
    }

    const start = this.pos;
    const ch = this.text.charCodeAt(this.pos);

    if (Number.isNaN(ch)) {
      return this.makeToken(TokenKind.EndOfFile, start, start, "", precededByLineBreak);
    }

    // Identifiers / keywords.
    if (isIdentifierStart(ch)) {
      return this.scanIdentifier(start, precededByLineBreak);
    }

    // Numeric literals.
    if (isDigit(ch)) {
      return this.scanNumber(start, precededByLineBreak);
    }
    if (ch === Char.Dot && isDigit(this.text.charCodeAt(this.pos + 1))) {
      return this.scanNumber(start, precededByLineBreak);
    }

    // String literals.
    if (ch === Char.SingleQuote || ch === Char.DoubleQuote) {
      return this.scanString(start, precededByLineBreak, ch);
    }

    // Template literals.
    if (ch === Char.Backtick) {
      return this.scanTemplate(start, precededByLineBreak, /* isHead */ true);
    }

    if (ch === Char.Hash) {
      this.pos++;
      const name = this.scanIdentifier(this.pos, precededByLineBreak);
      return this.makeToken(TokenKind.PrivateIdentifier, start, name.end, this.text.slice(start, name.end), precededByLineBreak);
    }

    return this.scanPunctuation(start, precededByLineBreak);
  }

  private makeToken(
    kind: TokenKind,
    start: number,
    end: number,
    text: string,
    precededByLineBreak: boolean,
    value?: string | number | bigint,
  ): Token {
    return { kind, start, end, text, value, precededByLineBreak };
  }

  private scanIdentifier(start: number, precededByLineBreak: boolean): Token {
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
  }

  private scanNumber(start: number, precededByLineBreak: boolean): Token {
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
  }

  private finishNumber(start: number, precededByLineBreak: boolean, isBigInt: boolean): Token {
    let raw = this.text.slice(start, this.pos);
    const cleaned = raw.replace(/_/g, "");
    if (isBigInt) {
      raw = raw.slice(0, -1).replace(/_/g, "");
      try {
        return this.makeToken(TokenKind.BigIntLiteral, start, this.pos, raw, precededByLineBreak, BigInt(raw));
      } catch {
        this.error(DiagnosticCode.InvalidNumber, `Invalid BigInt literal '${raw}'`, start, this.pos);
        return this.makeToken(TokenKind.BigIntLiteral, start, this.pos, raw, precededByLineBreak, 0n);
      }
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
  }

  private scanString(start: number, precededByLineBreak: boolean, quote: number): Token {
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
  }

  private scanEscapeSequence(): string {
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
  }

  /** Scans a template literal chunk starting at a backtick or after `}`. */
  scanTemplate(start: number, precededByLineBreak: boolean, isHead: boolean): Token {
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
  }

  /** Called by the parser after a template substitution expression. */
  continueTemplate(offset: number): Token {
    this.pos = Math.max(0, Math.min(offset, this.text.length));
    this._token = this.scanTemplate(offset, false, /* isHead */ false);
    return this._token;
  }

  /**
   * True when a `/` in this position starts a regular expression rather than a
   * division. The decision is made from the previous significant token.
   */
  private regexAllowed(): boolean {
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
  }

  /** Scan a `/pattern/flags` literal, starting at the leading slash. */
  private scanRegex(start: number, precededByLineBreak: boolean): Token {
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
  }

  private scanPunctuation(start: number, precededByLineBreak: boolean): Token {
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
  }
}
