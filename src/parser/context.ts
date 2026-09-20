/**
 * Shared state and low-level token plumbing for the parser.
 *
 * The parser's method groups (statements, expressions, types, modules) are
 * defined as separate modules and installed onto `Parser.prototype`. This class
 * holds the state they all operate on plus the token accessors every group
 * needs.
 */

import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import type { SourceFile } from "../diagnostics/source.js";
import { Scanner } from "../lexer/scanner.js";
import { isIdentifierNameToken, isKeywordKind, Token, TokenKind } from "../lexer/token.js";
import { SyntaxKind } from "../ast/kinds.js";
import type { Identifier, StringLiteral } from "../ast/nodes.js";
import { SpeculationError } from "./speculation.js";

export interface ParserState {
  index: number;
  tokens: Token[];
  pos: number;
  consumed: number;
}

export class ParserContext {
  readonly scanner: Scanner;
  readonly diagnostics: DiagnosticBag;
  readonly source: SourceFile;
  tokens: Token[];
  index = 0;
  /** Non-zero while a speculative parse is running; errors abort the attempt. */
  speculating = 0;
  /** Monotonic count of tokens consumed; used to detect lack of progress. */
  consumed = 0;

  constructor(source: SourceFile, diagnostics: DiagnosticBag) {
    this.source = source;
    this.diagnostics = diagnostics;
    this.scanner = new Scanner(source, diagnostics);
    this.tokens = [this.scanner.nextToken()];
  }

  // -- token access ---------------------------------------------------------

  get token(): Token {
    return this.tokens[this.index]!;
  }

  /**
   * Compare the current token kind without triggering TypeScript's control-flow
   * narrowing of `this.token.kind`, which would otherwise be invalidated by the
   * calls to `nextToken()` that occur between checks.
   */
  at(kind: TokenKind): boolean {
    return this.tokens[this.index]!.kind === kind;
  }

  atAhead(k: number, kind: TokenKind): boolean {
    return this.lookAhead(k).kind === kind;
  }

  lookAhead(k: number): Token {
    while (this.tokens.length <= this.index + k) {
      this.tokens.push(this.scanner.nextToken());
    }
    return this.tokens[this.index + k]!;
  }

  nextToken(): Token {
    this.consumed++;
    const current = this.token;
    if (this.index + 1 < this.tokens.length) {
      this.index++;
    } else {
      this.tokens = [this.scanner.nextToken()];
      this.index = 0;
    }
    return current;
  }

  parseExpected(kind: TokenKind, message?: string): Token {
    if (this.token.kind === kind) return this.nextToken();
    this.error(
      DiagnosticCode.ExpectedToken,
      message ?? `Expected '${kind}' but found '${this.token.text || this.token.kind}'`,
      this.token,
    );
    return this.token;
  }

  error(code: DiagnosticCode, message: string, token: Token = this.token): void {
    this.diagnostics.error(code, message, { start: token.start, end: token.end }, this.source.fileName);
    if (this.speculating > 0) throw new SpeculationError();
  }

  /**
   * Run `fn` speculatively. Any diagnostic raised while it runs (which aborts
   * the attempt via `SpeculationError`) is rolled back along with the scanner
   * position, so the parser can try a different production cleanly.
   */
  tryParse<T>(fn: () => T): T | undefined {
    const mark = this.diagnostics.mark();
    const state = this.saveState();
    this.speculating++;
    let result: T | undefined;
    let ok = false;
    try {
      result = fn();
      ok = true;
    } catch (error) {
      if (!(error instanceof SpeculationError)) throw error;
    } finally {
      this.speculating--;
    }
    if (!ok) {
      this.diagnostics.reset(mark);
      this.restoreState(state);
      return undefined;
    }
    return result;
  }

  isIdentifierLike(token: Token): boolean {
    return isIdentifierNameToken(token.kind);
  }

  parseIdentifier(allowKeywords = false): Identifier {
    const token = this.token;
    const ok = allowKeywords ? isKeywordKind(token.kind) : isIdentifierNameToken(token.kind);
    if (token.kind === TokenKind.Identifier || ok) {
      this.nextToken();
      return { kind: SyntaxKind.Identifier, text: token.text, start: token.start, end: token.end };
    }
    this.error(DiagnosticCode.ExpectedIdentifier, `Expected identifier but found '${token.text || token.kind}'`);
    if (token.kind !== TokenKind.EndOfFile) this.nextToken();
    return { kind: SyntaxKind.Identifier, text: token.text || "missing", start: token.start, end: token.end };
  }

  parseIdentifierName(): Identifier {
    return this.parseIdentifier(/* allowKeywords */ true);
  }

  parseSemicolon(): void {
    if (this.at(TokenKind.Semicolon)) {
      this.nextToken();
      return;
    }
    if (this.canInsertSemicolon()) return;
    this.error(DiagnosticCode.ExpectedToken, "Expected ';'", this.token);
  }

  canInsertSemicolon(): boolean {
    return this.at(TokenKind.EndOfFile) || this.at(TokenKind.CloseBrace) || this.token.precededByLineBreak;
  }

  parseDelimitedList<T>(close: TokenKind, parseElement: () => T): T[] {
    const items: T[] = [];
    while (this.token.kind !== close && !this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      items.push(parseElement());
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
      if (this.consumed === before && this.token.kind !== close) this.nextToken();
    }
    this.parseExpected(close);
    return items;
  }

  stringLiteralFromToken(token: Token): StringLiteral {
    return { kind: SyntaxKind.StringLiteral, text: token.text, raw: token.text, value: String(token.value ?? ""), start: token.start, end: token.end };
  }

  // -- state save / restore (for speculative parsing) -----------------------

  saveState(): ParserState {
    return { index: this.index, tokens: this.tokens.slice(), pos: this.scanner.position, consumed: this.consumed };
  }

  restoreState(state: ParserState): void {
    this.index = state.index;
    this.tokens = state.tokens;
    this.scanner.seek(state.pos);
    this.consumed = state.consumed;
  }
}
