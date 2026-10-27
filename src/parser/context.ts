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

/**
 * The template-literal context of the scanner at one point in the token
 * stream. `frames` holds the brace depth of every open `${ … }` substitution
 * (innermost last); `pendingOffset` is the `}` that must be re-read as template
 * text before the next token is scanned.
 */
interface TemplateState {
  readonly frames: readonly number[];
  readonly pendingOffset: number | undefined;
}

const OUTSIDE_TEMPLATE: TemplateState = { frames: [], pendingOffset: undefined };

export interface ParserState {
  index: number;
  tokens: Token[];
  pos: number;
  consumed: number;
  template: TemplateState;
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
  /** Brace depth of each open `${ … }` substitution, innermost last. */
  private templateFrames: number[] = [];
  /** Offset of the `}` that must be re-read as template text on the next scan. */
  private pendingTemplateOffset: number | undefined;
  /**
   * Template context after each buffered token. A parser method that rewinds
   * the scanner (the regex fallback) restores the context recorded on the
   * current token so a template that was only seen by lookahead stays coherent.
   */
  private readonly templateStates = new WeakMap<Token, TemplateState>();

  constructor(source: SourceFile, diagnostics: DiagnosticBag) {
    this.source = source;
    this.diagnostics = diagnostics;
    this.scanner = new Scanner(source, diagnostics);
    this.tokens = [this.scanToken()];
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
      this.tokens.push(this.scanToken());
    }
    return this.tokens[this.index + k]!;
  }

  nextToken(): Token {
    this.consumed++;
    const current = this.token;
    if (this.index + 1 < this.tokens.length) {
      this.index++;
    } else {
      this.tokens = [this.scanToken()];
      this.index = 0;
    }
    return current;
  }

  /**
   * Scan the next token, keeping a template literal together. A `${ … }`
   * substitution is ordinary token stream (so the parser can parse the
   * expression), but the `}` that closes it must be followed by a token that
   * re-reads the source as template text. Scanning the tail of a template as
   * normal tokens would let its raw text (quotes, parens, `}`) leak into
   * lookahead - and `findMatchingParen` would then count characters that are
   * part of the literal.
   */
  private scanToken(): Token {
    let token: Token;
    if (this.pendingTemplateOffset !== undefined) {
      const offset = this.pendingTemplateOffset;
      this.pendingTemplateOffset = undefined;
      token = this.scanner.continueTemplate(offset);
      if (token.kind === TokenKind.NoSubstitutionTemplateLiteral) {
        this.templateFrames.pop();
      } else if (token.kind === TokenKind.TemplateHead) {
        /* Another `${` in the same template restarts its brace depth. */
        const top = this.templateFrames.length - 1;
        if (top >= 0) this.templateFrames[top] = 0;
      }
    } else {
      token = this.scanner.nextToken();
      if (token.kind === TokenKind.TemplateHead) {
        this.templateFrames.push(0);
      } else if (this.templateFrames.length > 0) {
        const top = this.templateFrames.length - 1;
        if (token.kind === TokenKind.OpenBrace) {
          this.templateFrames[top] = this.templateFrames[top]! + 1;
        } else if (token.kind === TokenKind.CloseBrace) {
          if (this.templateFrames[top]! === 0) this.pendingTemplateOffset = token.start;
          else this.templateFrames[top] = this.templateFrames[top]! - 1;
        }
      }
    }
    if (this.templateFrames.length > 0 || this.pendingTemplateOffset !== undefined) {
      this.templateStates.set(token, this.snapshotTemplateState());
    }
    return token;
  }

  /**
   * Discard any lookahead and continue scanning at `offset`. Used when a
   * literal's raw text has to be re-read (the regex fallback). The template
   * context is restored to the state recorded for the current token, because
   * the skipped source cannot have opened or closed a template.
   */
  rescanFrom(offset: number): void {
    this.restoreTemplateState(this.templateStates.get(this.token) ?? OUTSIDE_TEMPLATE);
    this.scanner.seek(offset);
    this.tokens = [this.scanToken()];
    this.index = 0;
  }

  private snapshotTemplateState(): TemplateState {
    return { frames: this.templateFrames.slice(), pendingOffset: this.pendingTemplateOffset };
  }

  private restoreTemplateState(state: TemplateState): void {
    this.templateFrames = [...state.frames];
    this.pendingTemplateOffset = state.pendingOffset;
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
    return {
      index: this.index,
      tokens: this.tokens.slice(),
      pos: this.scanner.position,
      consumed: this.consumed,
      template: this.snapshotTemplateState(),
    };
  }

  restoreState(state: ParserState): void {
    this.index = state.index;
    this.tokens = state.tokens;
    this.scanner.seek(state.pos);
    this.consumed = state.consumed;
    this.restoreTemplateState(state.template);
  }
}
