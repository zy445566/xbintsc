import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import type { SourceFile } from "../diagnostics/source.js";
import { Token, TokenKind } from "./token.js";
import { Char, isDigit, isIdentifierStart, isLineBreak, isWhiteSpace } from "./char.js";
import { identifierMethods, type IdentifierMethods } from "./scanner/identifiers.js";
import { numberMethods, type NumberMethods } from "./scanner/numbers.js";
import { stringMethods, type StringMethods } from "./scanner/strings.js";
import { regexMethods, type RegexMethods } from "./scanner/regex.js";
import { punctuationMethods, type PunctuationMethods } from "./scanner/punctuation.js";

/**
 * Hand-written scanner for the TypeScript language. It produces a flat token
 * stream consumed by the parser. Comments and trivia are skipped, but line
 * breaks are tracked so the parser can implement ASI (automatic semicolon
 * insertion) correctly.
 *
 * The scanning routines are split by token category under `scanner/` and
 * merged onto the prototype below so each group can share the scanner state.
 */
export class Scanner {
  text: string;
  pos = 0;
  readonly diagnostics: DiagnosticBag;
  readonly source: SourceFile;
  _token: Token = {
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

  error(code: DiagnosticCode, message: string, start: number, end = this.pos): void {
    this.diagnostics.error(code, message, { start, end }, this.source.fileName);
  }

  scan(): Token {
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

  makeToken(
    kind: TokenKind,
    start: number,
    end: number,
    text: string,
    precededByLineBreak: boolean,
    value?: string | number | bigint,
  ): Token {
    return { kind, start, end, text, value, precededByLineBreak };
  }
}

export interface Scanner
  extends IdentifierMethods,
    NumberMethods,
    StringMethods,
    RegexMethods,
    PunctuationMethods {}

Object.assign(
  Scanner.prototype,
  identifierMethods,
  numberMethods,
  stringMethods,
  regexMethods,
  punctuationMethods,
);
