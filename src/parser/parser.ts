import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import type { SourceFile } from "../diagnostics/source.js";
import { Scanner } from "../lexer/scanner.js";
import { isOfKeyword, Token, TokenKind } from "../lexer/token.js";
import {
  AssignmentOperator,
  BinaryOperator,
  ModifierKind,
  NodeFlags,
  PostfixUnaryOperator,
  PrefixUnaryOperator,
  SyntaxKind,
  TypeOperator,
  type ArrowFunction,
  type ArrayLiteralExpression,
  type BinaryExpression,
  type Block,
  type BooleanLiteral,
  type CallExpression,
  type CaseClause,
  type CatchClause,
  type ClassDeclaration,
  type ClassElement,
  type ConditionalExpression,
  type ConstructorDeclaration,
  type DefaultClause,
  type EnumDeclaration,
  type EnumMember,
  type ExportAssignment,
  type ExportDeclaration,
  type ExportSpecifier,
  type Expression,
  type ExpressionWithTypeArguments,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type FunctionDeclaration,
  type FunctionExpression,
  type HeritageClause,
  type Identifier,
  type IfStatement,
  type ImportClause,
  type ImportDeclaration,
  type ImportSpecifier,
  type InterfaceDeclaration,
  type MethodDeclaration,
  type MethodSignature,
  type Modifier,
  type ModuleDeclaration,
  type NamedExports,
  type NamedImports,
  type NamespaceImport,
  type NewExpression,
  type NumericLiteral,
  type ObjectLiteralElementLike,
  type ObjectLiteralExpression,
  type ParenthesizedExpression,
  type Parameter,
  type PropertyDeclaration,
  type PropertyName,
  type PropertySignature,
  type RegularExpressionLiteral,
  type ShorthandPropertyAssignment,
  type SourceFileNode,
  type Statement,
  type StringLiteral,
  type SwitchStatement,
  type TemplateLiteral,
  type TemplateSpan,
  type TryStatement,
  type TypeAliasDeclaration,
  type TypeElement,
  type TypeNode,
  type TypeParameterDeclaration,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";

/**
 * Recursive-descent parser for TypeScript.
 *
 * Type syntax is parsed for structure but erased before code generation; the
 * parser is therefore permissive about types while the backend focuses on the
 * value-level language.
 */
export class Parser {
  private readonly scanner: Scanner;
  private readonly diagnostics: DiagnosticBag;
  private readonly source: SourceFile;
  private tokens: Token[];
  private index = 0;
  /** Non-zero while a speculative parse is running; errors abort the attempt. */
  private speculating = 0;
  /** Monotonic count of tokens consumed; used to detect lack of progress. */
  private consumed = 0;

  constructor(source: SourceFile, diagnostics: DiagnosticBag) {
    this.source = source;
    this.diagnostics = diagnostics;
    this.scanner = new Scanner(source, diagnostics);
    this.tokens = [this.scanner.nextToken()];
  }

  // -- token access ---------------------------------------------------------

  private get token(): Token {
    return this.tokens[this.index]!;
  }

  /**
   * Compare the current token kind without triggering TypeScript's control-flow
   * narrowing of `this.token.kind`, which would otherwise be invalidated by the
   * calls to `nextToken()` that occur between checks.
   */
  private at(kind: TokenKind): boolean {
    return this.tokens[this.index]!.kind === kind;
  }

  private atAhead(k: number, kind: TokenKind): boolean {
    return this.lookAhead(k).kind === kind;
  }

  private lookAhead(k: number): Token {
    while (this.tokens.length <= this.index + k) {
      this.tokens.push(this.scanner.nextToken());
    }
    return this.tokens[this.index + k]!;
  }

  private nextToken(): Token {
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

  private parseExpected(kind: TokenKind, message?: string): Token {
    if (this.token.kind === kind) return this.nextToken();
    this.error(
      DiagnosticCode.ExpectedToken,
      message ?? `Expected '${kind}' but found '${this.token.text || this.token.kind}'`,
      this.token,
    );
    return this.token;
  }

  private error(code: DiagnosticCode, message: string, token: Token = this.token): void {
    this.diagnostics.error(code, message, { start: token.start, end: token.end }, this.source.fileName);
    if (this.speculating > 0) throw new SpeculationError();
  }

  /**
   * Run `fn` speculatively. Any diagnostic raised while it runs (which aborts
   * the attempt via `SpeculationError`) is rolled back along with the scanner
   * position, so the parser can try a different production cleanly.
   */
  private tryParse<T>(fn: () => T): T | undefined {
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

  private isIdentifierLike(token: Token): boolean {
    if (token.kind === TokenKind.Identifier) return true;
    switch (token.kind) {
      case TokenKind.AsKeyword:
      case TokenKind.SatisfiesKeyword:
      case TokenKind.FromKeyword:
      case TokenKind.TypeKeyword:
      case TokenKind.GetKeyword:
      case TokenKind.SetKeyword:
      case TokenKind.AsyncKeyword:
        return true;
      default:
        return false;
    }
  }

  private parseIdentifier(allowKeywords = false): Identifier {
    const token = this.token;
    if (token.kind === TokenKind.Identifier || (allowKeywords && this.isIdentifierLike(token))) {
      this.nextToken();
      return { kind: SyntaxKind.Identifier, text: token.text, start: token.start, end: token.end };
    }
    this.error(DiagnosticCode.ExpectedIdentifier, `Expected identifier but found '${token.text || token.kind}'`);
    if (token.kind !== TokenKind.EndOfFile) this.nextToken();
    return { kind: SyntaxKind.Identifier, text: token.text || "missing", start: token.start, end: token.end };
  }

  private parseIdentifierName(): Identifier {
    return this.parseIdentifier(/* allowKeywords */ true);
  }

  private parseSemicolon(): void {
    if (this.at(TokenKind.Semicolon)) {
      this.nextToken();
      return;
    }
    if (this.canInsertSemicolon()) return;
    this.error(DiagnosticCode.ExpectedToken, "Expected ';'", this.token);
  }

  private canInsertSemicolon(): boolean {
    return this.at(TokenKind.EndOfFile) || this.at(TokenKind.CloseBrace) || this.token.precededByLineBreak;
  }

  private parseDelimitedList<T>(close: TokenKind, parseElement: () => T): T[] {
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

  // -- entry point ----------------------------------------------------------

  parseSourceFile(): SourceFileNode {
    const statements: Statement[] = [];
    while (!this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const beforePos = this.scanner.position;
      const statement = this.parseStatement();
      if (statement) statements.push(statement);
      if (this.consumed === before) {
        this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${this.token.text}'`, this.token);
        this.nextToken();
      }
    }
    return {
      kind: SyntaxKind.SourceFile,
      statements,
      fileName: this.source.fileName,
      text: this.source.text,
      start: 0,
      end: this.source.text.length,
    };
  }

  // -- modifiers ------------------------------------------------------------

  private tryParseModifiers(): Modifier[] {
    const modifiers: Modifier[] = [];
    for (;;) {
      const kind = this.token.kind;
      let modifierKind: ModifierKind | undefined;
      switch (kind) {
        case TokenKind.ExportKeyword:
          modifierKind = ModifierKind.Export;
          break;
        case TokenKind.DefaultKeyword:
          if (this.atAhead(1, TokenKind.FunctionKeyword) || this.atAhead(1, TokenKind.ClassKeyword)) {
            modifierKind = ModifierKind.Default;
          }
          break;
        case TokenKind.DeclareKeyword:
          modifierKind = ModifierKind.Declare;
          break;
        case TokenKind.AbstractKeyword:
          modifierKind = ModifierKind.Abstract;
          break;
        case TokenKind.PublicKeyword:
          modifierKind = ModifierKind.Public;
          break;
        case TokenKind.PrivateKeyword:
          modifierKind = ModifierKind.Private;
          break;
        case TokenKind.ProtectedKeyword:
          modifierKind = ModifierKind.Protected;
          break;
        case TokenKind.StaticKeyword:
          modifierKind = ModifierKind.Static;
          break;
        case TokenKind.ReadonlyKeyword:
          modifierKind = ModifierKind.Readonly;
          break;
        case TokenKind.AsyncKeyword:
          if (!this.lookAhead(1).precededByLineBreak) modifierKind = ModifierKind.Async;
          break;
        default:
          break;
      }
      if (!modifierKind) break;
      const token = this.nextToken();
      modifiers.push({ kind: SyntaxKind.Unknown, modifierKind, start: token.start, end: token.end });
    }
    return modifiers;
  }

  private hasModifier(modifiers: readonly Modifier[], kind: ModifierKind): boolean {
    return modifiers.some((m) => m.modifierKind === kind);
  }

  // -- statements -----------------------------------------------------------

  private parseStatement(): Statement | undefined {
    const token = this.token;
    switch (token.kind) {
      case TokenKind.OpenBrace:
        return this.parseBlock();
      case TokenKind.Semicolon: {
        const t = this.nextToken();
        return { kind: SyntaxKind.EmptyStatement, start: t.start, end: t.end } as Statement;
      }
      case TokenKind.VarKeyword:
      case TokenKind.LetKeyword:
      case TokenKind.ConstKeyword:
        return this.parseVariableStatement([]);
      case TokenKind.FunctionKeyword:
        return this.parseFunctionDeclaration([]);
      case TokenKind.AsyncKeyword:
        if (this.atAhead(1, TokenKind.FunctionKeyword) && !this.lookAhead(1).precededByLineBreak) {
          const asyncMod = this.tryParseModifiers();
          return this.parseFunctionDeclaration(asyncMod);
        }
        break;
      case TokenKind.ClassKeyword:
        return this.parseClassDeclaration([]);
      case TokenKind.IfKeyword:
        return this.parseIfStatement();
      case TokenKind.WhileKeyword:
        return this.parseWhileStatement();
      case TokenKind.DoKeyword:
        return this.parseDoStatement();
      case TokenKind.ForKeyword:
        return this.parseForStatement();
      case TokenKind.ReturnKeyword:
        return this.parseReturnStatement();
      case TokenKind.BreakKeyword:
        return this.parseBreakStatement();
      case TokenKind.ContinueKeyword:
        return this.parseContinueStatement();
      case TokenKind.ThrowKeyword:
        return this.parseThrowStatement();
      case TokenKind.TryKeyword:
        return this.parseTryStatement();
      case TokenKind.SwitchKeyword:
        return this.parseSwitchStatement();
      case TokenKind.DebuggerKeyword: {
        const t = this.nextToken();
        this.parseSemicolon();
        return { kind: SyntaxKind.DebuggerStatement, start: t.start, end: t.end } as Statement;
      }
      case TokenKind.ImportKeyword:
        if (this.atAhead(1, TokenKind.OpenParen) || this.atAhead(1, TokenKind.Dot)) break;
        return this.parseImportDeclaration();
      case TokenKind.ExportKeyword:
        return this.parseExportDeclaration();
      case TokenKind.InterfaceKeyword:
        return this.parseInterfaceDeclaration([]);
      case TokenKind.TypeKeyword:
        if (this.isIdentifierLike(this.lookAhead(1))) return this.parseTypeAliasDeclaration([]);
        break;
      case TokenKind.EnumKeyword:
        return this.parseEnumDeclaration([]);
      case TokenKind.NamespaceKeyword:
      case TokenKind.ModuleKeyword:
        return this.parseModuleDeclaration([]);
      case TokenKind.DeclareKeyword: {
        const modifiers = this.tryParseModifiers();
        return this.parseStatementWithModifiers(modifiers);
      }
      default:
        break;
    }
    return this.parseExpressionStatement();
  }

  private parseStatementWithModifiers(modifiers: Modifier[]): Statement {
    switch (this.token.kind) {
      case TokenKind.FunctionKeyword:
        return this.parseFunctionDeclaration(modifiers);
      case TokenKind.ClassKeyword:
        return this.parseClassDeclaration(modifiers);
      case TokenKind.InterfaceKeyword:
        return this.parseInterfaceDeclaration(modifiers);
      case TokenKind.TypeKeyword:
        return this.parseTypeAliasDeclaration(modifiers);
      case TokenKind.EnumKeyword:
        return this.parseEnumDeclaration(modifiers);
      case TokenKind.NamespaceKeyword:
      case TokenKind.ModuleKeyword:
        return this.parseModuleDeclaration(modifiers);
      default:
        return this.parseExpressionStatement();
    }
  }

  private parseBlock(): Block {
    const open = this.parseExpected(TokenKind.OpenBrace);
    const statements: Statement[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const beforePos = this.scanner.position;
      const s = this.parseStatement();
      if (s) statements.push(s);
      if (this.consumed === before) {
        this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${this.token.text}'`, this.token);
        this.nextToken();
      }
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.Block, statements, start: open.start, end: close.end };
  }

  private parseVariableStatement(modifiers: Modifier[]): VariableStatement {
    const declList = this.parseVariableDeclarationList();
    this.parseSemicolon();
    return { kind: SyntaxKind.VariableStatement, declarationList: declList, modifiers, start: declList.start, end: declList.end };
  }

  private parseVariableDeclarationList(): VariableDeclarationList {
    const keyword = this.nextToken();
    const declarationKind = keyword.kind === TokenKind.ConstKeyword ? "const" : keyword.kind === TokenKind.LetKeyword ? "let" : "var";
    const declarations: VariableDeclaration[] = [];
    do {
      declarations.push(this.parseVariableDeclaration());
    } while (this.at(TokenKind.Comma) && this.nextToken());
    return {
      kind: SyntaxKind.VariableDeclarationList,
      declarations,
      declarationKind,
      start: keyword.start,
      end: declarations[declarations.length - 1]?.end ?? keyword.end,
    };
  }

  private parseVariableDeclaration(): VariableDeclaration {
    const name = this.parseIdentifier();
    let exclamation = false;
    if (this.at(TokenKind.Exclamation)) {
      this.nextToken();
      exclamation = true;
    }
    let type: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      type = this.parseType();
    }
    let initializer: Expression | undefined;
    if (this.at(TokenKind.Equals)) {
      this.nextToken();
      initializer = this.parseAssignmentExpression();
    }
    return {
      kind: SyntaxKind.VariableDeclaration,
      name,
      exclamation,
      type,
      initializer,
      start: name.start,
      end: initializer?.end ?? type?.end ?? name.end,
    };
  }

  private parseIfStatement(): IfStatement {
    const start = this.parseExpected(TokenKind.IfKeyword).start;
    this.parseExpected(TokenKind.OpenParen);
    const condition = this.parseExpression();
    this.parseExpected(TokenKind.CloseParen);
    const thenStatement = this.parseStatement()!;
    let elseStatement: Statement | undefined;
    if (this.at(TokenKind.ElseKeyword)) {
      this.nextToken();
      elseStatement = this.parseStatement();
    }
    return { kind: SyntaxKind.IfStatement, condition, thenStatement, elseStatement, start, end: elseStatement?.end ?? thenStatement.end };
  }

  private parseWhileStatement(): Statement {
    const start = this.parseExpected(TokenKind.WhileKeyword).start;
    this.parseExpected(TokenKind.OpenParen);
    const condition = this.parseExpression();
    this.parseExpected(TokenKind.CloseParen);
    const statement = this.parseStatement()!;
    return { kind: SyntaxKind.WhileStatement, condition, statement, start, end: statement.end } as Statement;
  }

  private parseDoStatement(): Statement {
    const start = this.parseExpected(TokenKind.DoKeyword).start;
    const statement = this.parseStatement()!;
    this.parseExpected(TokenKind.WhileKeyword);
    this.parseExpected(TokenKind.OpenParen);
    const condition = this.parseExpression();
    const close = this.parseExpected(TokenKind.CloseParen);
    this.parseSemicolon();
    return { kind: SyntaxKind.DoStatement, condition, statement, start, end: close.end } as Statement;
  }

  private parseForStatement(): ForStatement | ForOfStatement | ForInStatement {
    const start = this.parseExpected(TokenKind.ForKeyword).start;
    let awaitModifier = false;
    if (this.at(TokenKind.AwaitKeyword)) {
      this.nextToken();
      awaitModifier = true;
    }
    this.parseExpected(TokenKind.OpenParen);

    let initializer: VariableDeclarationList | Expression | undefined;
    if (!this.at(TokenKind.Semicolon)) {
      if (this.at(TokenKind.VarKeyword) || this.at(TokenKind.LetKeyword) || this.at(TokenKind.ConstKeyword)) {
        initializer = this.parseVariableDeclarationList();
      } else {
        initializer = this.parseExpression(/* noIn */ true);
      }
    }

    if (isOfKeyword(this.token)) {
      this.nextToken();
      const expression = this.parseAssignmentExpression();
      this.parseExpected(TokenKind.CloseParen);
      const statement = this.parseStatement()!;
      return { kind: SyntaxKind.ForOfStatement, initializer: initializer!, expression, statement, awaitModifier, start, end: statement.end };
    }
    if (this.at(TokenKind.InKeyword)) {
      this.nextToken();
      const expression = this.parseExpression();
      this.parseExpected(TokenKind.CloseParen);
      const statement = this.parseStatement()!;
      return { kind: SyntaxKind.ForInStatement, initializer: initializer!, expression, statement, start, end: statement.end };
    }

    this.parseExpected(TokenKind.Semicolon);
    let condition: Expression | undefined;
    if (!this.at(TokenKind.Semicolon)) condition = this.parseExpression();
    this.parseExpected(TokenKind.Semicolon);
    let incrementor: Expression | undefined;
    if (!this.at(TokenKind.CloseParen)) incrementor = this.parseExpression();
    this.parseExpected(TokenKind.CloseParen);
    const statement = this.parseStatement()!;
    return { kind: SyntaxKind.ForStatement, initializer, condition, incrementor, statement, start, end: statement.end };
  }

  private parseReturnStatement(): Statement {
    const start = this.parseExpected(TokenKind.ReturnKeyword).start;
    let expression: Expression | undefined;
    if (!this.at(TokenKind.Semicolon) && !this.canInsertSemicolon()) expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ReturnStatement, expression, start, end: expression?.end ?? start + 6 } as Statement;
  }

  private parseBreakStatement(): Statement {
    const start = this.parseExpected(TokenKind.BreakKeyword).start;
    let label: Identifier | undefined;
    if (!this.canInsertSemicolon() && !this.at(TokenKind.Semicolon)) label = this.parseIdentifier();
    this.parseSemicolon();
    return { kind: SyntaxKind.BreakStatement, label, start, end: label?.end ?? start + 5 } as Statement;
  }

  private parseContinueStatement(): Statement {
    const start = this.parseExpected(TokenKind.ContinueKeyword).start;
    let label: Identifier | undefined;
    if (!this.canInsertSemicolon() && !this.at(TokenKind.Semicolon)) label = this.parseIdentifier();
    this.parseSemicolon();
    return { kind: SyntaxKind.ContinueStatement, label, start, end: label?.end ?? start + 8 } as Statement;
  }

  private parseThrowStatement(): Statement {
    const start = this.parseExpected(TokenKind.ThrowKeyword).start;
    const expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ThrowStatement, expression, start, end: expression.end } as Statement;
  }

  private parseTryStatement(): TryStatement {
    const start = this.parseExpected(TokenKind.TryKeyword).start;
    const tryBlock = this.parseBlock();
    let catchClause: CatchClause | undefined;
    if (this.at(TokenKind.CatchKeyword)) {
      const catchStart = this.nextToken().start;
      let variable: Identifier | undefined;
      let type: TypeNode | undefined;
      if (this.at(TokenKind.OpenParen)) {
        this.nextToken();
        variable = this.parseIdentifier();
        if (this.at(TokenKind.Colon)) {
          this.nextToken();
          type = this.parseType();
        }
        this.parseExpected(TokenKind.CloseParen);
      }
      const block = this.parseBlock();
      catchClause = { kind: SyntaxKind.CatchClause, variable, type, block, start: catchStart, end: block.end };
    }
    let finallyBlock: Block | undefined;
    if (this.at(TokenKind.FinallyKeyword)) {
      this.nextToken();
      finallyBlock = this.parseBlock();
    }
    return { kind: SyntaxKind.TryStatement, tryBlock, catchClause, finallyBlock, start, end: finallyBlock?.end ?? catchClause?.end ?? tryBlock.end };
  }

  private parseSwitchStatement(): SwitchStatement {
    const start = this.parseExpected(TokenKind.SwitchKeyword).start;
    this.parseExpected(TokenKind.OpenParen);
    const expression = this.parseExpression();
    this.parseExpected(TokenKind.CloseParen);
    this.parseExpected(TokenKind.OpenBrace);
    const clauses: (CaseClause | DefaultClause)[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      if (this.at(TokenKind.CaseKeyword)) {
        const caseStart = this.nextToken().start;
        const caseExpression = this.parseExpression();
        this.parseExpected(TokenKind.Colon);
        const statements: Statement[] = [];
        while (
          !this.at(TokenKind.CaseKeyword) &&
          !this.at(TokenKind.DefaultKeyword) &&
          !this.at(TokenKind.CloseBrace) &&
          !this.at(TokenKind.EndOfFile)
        ) {
          const s = this.parseStatement();
          if (s) statements.push(s);
        }
        clauses.push({ kind: SyntaxKind.CaseClause, expression: caseExpression, statements, start: caseStart, end: statements[statements.length - 1]?.end ?? caseExpression.end });
      } else if (this.at(TokenKind.DefaultKeyword)) {
        const defaultStart = this.nextToken().start;
        this.parseExpected(TokenKind.Colon);
        const statements: Statement[] = [];
        while (!this.at(TokenKind.CaseKeyword) && !this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
          const s = this.parseStatement();
          if (s) statements.push(s);
        }
        clauses.push({ kind: SyntaxKind.DefaultClause, statements, start: defaultStart, end: statements[statements.length - 1]?.end ?? defaultStart });
      } else {
        this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${this.token.text}' in switch body`);
        this.nextToken();
      }
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.SwitchStatement, expression, clauses, start, end: close.end };
  }

  private parseExpressionStatement(): Statement {
    const expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ExpressionStatement, expression, start: expression.start, end: expression.end };
  }

  // -- declarations ---------------------------------------------------------

  private parseFunctionDeclaration(modifiers: Modifier[]): FunctionDeclaration {
    const start = this.parseExpected(TokenKind.FunctionKeyword).start;
    let flags = NodeFlags.None;
    if (this.at(TokenKind.Asterisk)) {
      this.nextToken();
      flags |= NodeFlags.Generator;
    }
    if (this.hasModifier(modifiers, ModifierKind.Async)) flags |= NodeFlags.Async;
    if (this.hasModifier(modifiers, ModifierKind.Declare)) flags |= NodeFlags.Declare;
    let name: Identifier | undefined;
    if (!this.at(TokenKind.OpenParen) && !this.at(TokenKind.LessThan)) name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const parameters = this.parseParameters();
    let returnType: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      returnType = this.parseReturnType();
    }
    let body: Block | undefined;
    if (this.at(TokenKind.OpenBrace)) body = this.parseBlock();
    else this.parseSemicolon();
    return {
      kind: SyntaxKind.FunctionDeclaration,
      name,
      modifiers,
      typeParameters,
      parameters,
      returnType,
      body,
      flags,
      start,
      end: body?.end ?? returnType?.end ?? parameters[parameters.length - 1]?.end ?? start,
    };
  }

  private parseTypeParameters(): TypeParameterDeclaration[] {
    if (!this.at(TokenKind.LessThan)) return [];
    this.nextToken();
    const params: TypeParameterDeclaration[] = [];
    while (!this.at(TokenKind.GreaterThan) && !this.at(TokenKind.EndOfFile)) {
      const name = this.parseIdentifier();
      let constraint: TypeNode | undefined;
      if (this.at(TokenKind.ExtendsKeyword)) {
        this.nextToken();
        constraint = this.parseType();
      }
      let defaultType: TypeNode | undefined;
      if (this.at(TokenKind.Equals)) {
        this.nextToken();
        defaultType = this.parseType();
      }
      params.push({ kind: SyntaxKind.TypeParameterDeclaration, name, constraint, default: defaultType, start: name.start, end: defaultType?.end ?? constraint?.end ?? name.end });
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseGreaterThan();
    return params;
  }

  /** Consume a `>` that may have been lexed as `>>` / `>>>`, splitting it. */
  private parseGreaterThan(): void {
    const token = this.token;
    if (token.kind === TokenKind.GreaterThan) {
      this.nextToken();
      return;
    }
    if (token.kind === TokenKind.GreaterThanGreaterThan || token.kind === TokenKind.GreaterThanGreaterThanGreaterThan) {
      const extra = token.kind === TokenKind.GreaterThanGreaterThan ? 1 : 2;
      this.tokens[this.index] = {
        kind: extra === 1 ? TokenKind.GreaterThan : TokenKind.GreaterThanGreaterThan,
        start: token.start + 1,
        end: token.end,
        text: token.text.slice(1),
        precededByLineBreak: false,
      };
      return;
    }
    this.parseExpected(TokenKind.GreaterThan);
  }

  private parseParameters(): Parameter[] {
    this.parseExpected(TokenKind.OpenParen);
    const params: Parameter[] = [];
    while (!this.at(TokenKind.CloseParen) && !this.at(TokenKind.EndOfFile)) {
      const start = this.token.start;
      const modifiers = this.tryParseModifiers();
      let dotDotDotToken = false;
      if (this.at(TokenKind.DotDotDot)) {
        this.nextToken();
        dotDotDotToken = true;
      }
      const name = this.parseIdentifier();
      let questionToken = false;
      if (this.at(TokenKind.Question)) {
        this.nextToken();
        questionToken = true;
      }
      let type: TypeNode | undefined;
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        type = this.parseType();
      }
      let initializer: Expression | undefined;
      if (this.at(TokenKind.Equals)) {
        this.nextToken();
        initializer = this.parseAssignmentExpression();
      }
      params.push({ kind: SyntaxKind.Parameter, name, modifiers, dotDotDotToken, questionToken, type, initializer, start, end: initializer?.end ?? type?.end ?? name.end });
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseExpected(TokenKind.CloseParen);
    return params;
  }

  private parseReturnType(): TypeNode {
    if (this.at(TokenKind.AssertsKeyword)) {
      const start = this.nextToken().start;
      let paramName: Identifier | undefined;
      if (this.isIdentifierLike(this.token) || this.at(TokenKind.ThisKeyword)) paramName = this.parseIdentifierName();
      let predicateType: TypeNode | undefined;
      if (this.at(TokenKind.IsKeyword)) {
        this.nextToken();
        predicateType = this.parseType();
      }
      const stub: Identifier = { kind: SyntaxKind.Identifier, text: paramName?.text ?? "this", start: paramName?.start ?? start, end: paramName?.end ?? start };
      return { kind: SyntaxKind.TypePredicate, parameterName: stub, type: predicateType, start, end: predicateType?.end ?? start };
    }
    if (this.isIdentifierLike(this.token) && this.atAhead(1, TokenKind.IsKeyword)) {
      const paramName = this.parseIdentifierName();
      this.nextToken();
      const predicateType = this.parseType();
      return { kind: SyntaxKind.TypePredicate, parameterName: paramName, type: predicateType, start: paramName.start, end: predicateType.end };
    }
    return this.parseType();
  }

  private parseClassDeclaration(modifiers: Modifier[]): ClassDeclaration {
    const start = this.parseExpected(TokenKind.ClassKeyword).start;
    let name: Identifier | undefined;
    if (!this.at(TokenKind.OpenBrace) && !this.at(TokenKind.ExtendsKeyword) && !this.at(TokenKind.ImplementsKeyword)) {
      name = this.parseIdentifier();
    }
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseClassMembers();
    return { kind: SyntaxKind.ClassDeclaration, name, modifiers, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? start };
  }

  private parseHeritageClauses(): HeritageClause[] {
    const clauses: HeritageClause[] = [];
    while (this.at(TokenKind.ExtendsKeyword) || this.at(TokenKind.ImplementsKeyword)) {
      const token = this.nextToken();
      const tokenKind: "extends" | "implements" = token.kind === TokenKind.ExtendsKeyword ? "extends" : "implements";
      const types: ExpressionWithTypeArguments[] = [];
      do {
        const expression = this.parseLeftHandSideExpression();
        const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
        types.push({ kind: SyntaxKind.ExpressionWithTypeArguments, expression, typeArguments, start: expression.start, end: typeArguments[typeArguments.length - 1]?.end ?? expression.end });
      } while (this.at(TokenKind.Comma) && this.nextToken());
      clauses.push({ kind: SyntaxKind.HeritageClause, token: tokenKind, types, start: token.start, end: types[types.length - 1]?.end ?? token.end });
    }
    return clauses;
  }

  private parseClassMembers(): ClassElement[] {
    this.parseExpected(TokenKind.OpenBrace);
    const members: ClassElement[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const member = this.parseClassElement();
      if (member) members.push(member);
      if (this.consumed === before) this.nextToken();
    }
    this.parseExpected(TokenKind.CloseBrace);
    return members;
  }

  private parseClassElement(): ClassElement | undefined {
    const start = this.token.start;
    const modifiers = this.tryParseModifiers();
    if (this.at(TokenKind.Semicolon)) {
      this.nextToken();
      return undefined;
    }
    if (this.at(TokenKind.ConstructorKeyword)) {
      const name = this.nextToken();
      const parameters = this.parseParameters();
      let body: Block | undefined;
      if (this.at(TokenKind.OpenBrace)) body = this.parseBlock();
      else this.parseSemicolon();
      const ctor: ConstructorDeclaration = { kind: SyntaxKind.ConstructorDeclaration, modifiers, parameters, body, start, end: body?.end ?? name.end };
      return ctor;
    }
    let flags = NodeFlags.None;
    if (this.at(TokenKind.Asterisk)) {
      this.nextToken();
      flags |= NodeFlags.Generator;
    }
    if (this.hasModifier(modifiers, ModifierKind.Async)) flags |= NodeFlags.Async;
    const name = this.parsePropertyName();
    let questionToken = false;
    let exclamationToken = false;
    if (this.at(TokenKind.Question)) {
      this.nextToken();
      questionToken = true;
    }
    if (this.at(TokenKind.Exclamation)) {
      this.nextToken();
      exclamationToken = true;
    }
    const typeParameters = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
    if (this.at(TokenKind.OpenParen)) {
      const parameters = this.parseParameters();
      let returnType: TypeNode | undefined;
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        returnType = this.parseReturnType();
      }
      let body: Block | undefined;
      if (this.at(TokenKind.OpenBrace)) body = this.parseBlock();
      else this.parseSemicolon();
      const method: MethodDeclaration = {
        kind: SyntaxKind.MethodDeclaration,
        name,
        modifiers,
        typeParameters,
        parameters,
        returnType,
        body,
        flags,
        optional: questionToken,
        start,
        end: body?.end ?? returnType?.end ?? name.end,
      };
      return method;
    }
    let type: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      type = this.parseType();
    }
    let initializer: Expression | undefined;
    if (this.at(TokenKind.Equals)) {
      this.nextToken();
      initializer = this.parseAssignmentExpression();
    }
    this.parseSemicolon();
    const property: PropertyDeclaration = {
      kind: SyntaxKind.PropertyDeclaration,
      name,
      modifiers,
      questionToken,
      exclamationToken,
      type,
      initializer,
      start,
      end: initializer?.end ?? type?.end ?? name.end,
    };
    return property;
  }

  private parsePropertyName(): PropertyName {
    const token = this.token;
    if (token.kind === TokenKind.StringLiteral) {
      this.nextToken();
      return { kind: SyntaxKind.StringLiteral, text: token.text, raw: token.text, value: String(token.value ?? ""), start: token.start, end: token.end };
    }
    if (token.kind === TokenKind.NumericLiteral) {
      this.nextToken();
      return { kind: SyntaxKind.NumericLiteral, text: token.text, value: Number(token.value ?? 0), start: token.start, end: token.end };
    }
    if (token.kind === TokenKind.OpenBracket) {
      this.nextToken();
      const expr = this.parseAssignmentExpression();
      this.parseExpected(TokenKind.CloseBracket);
      const name = expressionToPropertyName(expr);
      if (name) return name;
      this.error(DiagnosticCode.InvalidTypeSyntax, "Computed property names are not supported by xbtsc");
      return { kind: SyntaxKind.Identifier, text: "<computed>", start: expr.start, end: expr.end };
    }
    return this.parseIdentifierName();
  }

  private parseInterfaceDeclaration(modifiers: Modifier[]): InterfaceDeclaration {
    const start = this.parseExpected(TokenKind.InterfaceKeyword).start;
    const name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseTypeMembers();
    return { kind: SyntaxKind.InterfaceDeclaration, name, modifiers, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? name.end };
  }

  private parseTypeAliasDeclaration(modifiers: Modifier[]): TypeAliasDeclaration {
    const start = this.parseExpected(TokenKind.TypeKeyword).start;
    const name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    this.parseExpected(TokenKind.Equals);
    const type = this.parseType();
    this.parseSemicolon();
    return { kind: SyntaxKind.TypeAliasDeclaration, name, modifiers, typeParameters, type, start, end: type.end };
  }

  private parseEnumDeclaration(modifiers: Modifier[]): EnumDeclaration {
    const start = this.parseExpected(TokenKind.EnumKeyword).start;
    const name = this.parseIdentifier();
    this.parseExpected(TokenKind.OpenBrace);
    const members: EnumMember[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const memberName = this.parsePropertyName();
      let initializer: Expression | undefined;
      if (this.at(TokenKind.Equals)) {
        this.nextToken();
        initializer = this.parseAssignmentExpression();
      }
      members.push({ kind: SyntaxKind.EnumMember, name: memberName, initializer, start: memberName.start, end: initializer?.end ?? memberName.end });
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.EnumDeclaration, name, modifiers, members, start, end: close.end };
  }

  private parseModuleDeclaration(modifiers: Modifier[]): ModuleDeclaration {
    const start = this.nextToken().start;
    let name: Identifier | StringLiteral;
    if (this.at(TokenKind.StringLiteral)) {
      const token = this.nextToken();
      name = { kind: SyntaxKind.StringLiteral, text: token.text, raw: token.text, value: String(token.value ?? ""), start: token.start, end: token.end };
    } else {
      name = this.parseIdentifierName();
    }
    while (this.at(TokenKind.Dot)) {
      this.nextToken();
      this.parseIdentifierName();
    }
    let body: Block | undefined;
    if (this.at(TokenKind.OpenBrace)) body = this.parseBlock();
    else this.parseSemicolon();
    return { kind: SyntaxKind.ModuleDeclaration, name, modifiers, body, start, end: body?.end ?? name.end };
  }

  private parseTypeMembers(): TypeElement[] {
    this.parseExpected(TokenKind.OpenBrace);
    const members: TypeElement[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const member = this.parseTypeMember();
      if (member) members.push(member);
      if (this.consumed === before) this.nextToken();
    }
    this.parseExpected(TokenKind.CloseBrace);
    return members;
  }

  private parseTypeMember(): TypeElement | undefined {
    const start = this.token.start;
    const readonlyToken = this.at(TokenKind.ReadonlyKeyword);
    if (readonlyToken) this.nextToken();
    if (this.at(TokenKind.OpenParen) || this.at(TokenKind.LessThan)) {
      const typeParameters = this.parseTypeParameters();
      const parameters = this.parseParameters();
      let returnType: TypeNode | undefined;
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        returnType = this.parseType();
      }
      this.parseSemicolonOrComma();
      const name: Identifier = { kind: SyntaxKind.Identifier, text: "__call", start, end: start };
      return { kind: SyntaxKind.MethodSignature, name, questionToken: false, typeParameters, parameters, returnType, start, end: returnType?.end ?? start };
    }
    if (this.at(TokenKind.OpenBracket) && this.atAhead(1, TokenKind.Identifier)) {
      const saveIndex = this.index;
      const savePos = this.scanner.position;
      const savedTokens = this.tokens.slice();
      this.nextToken();
      const parameters: Parameter[] = [];
      let ok = true;
      try {
        const paramName = this.parseIdentifier();
        let type: TypeNode | undefined;
        if (this.at(TokenKind.Colon)) {
          this.nextToken();
          type = this.parseType();
        } else ok = false;
        this.parseExpected(TokenKind.CloseBracket);
        parameters.push({ kind: SyntaxKind.Parameter, name: paramName, modifiers: [], dotDotDotToken: false, questionToken: false, type, start: paramName.start, end: type?.end ?? paramName.end });
      } catch {
        ok = false;
      }
      if (ok && this.at(TokenKind.Colon)) {
        this.nextToken();
        const type = this.parseType();
        this.parseSemicolonOrComma();
        return { kind: SyntaxKind.IndexSignature, parameters, type, start, end: type.end };
      }
      this.index = saveIndex;
      this.tokens = savedTokens;
      this.scanner.seek(savePos);
    }
    const name = this.parsePropertyName();
    let questionToken = false;
    if (this.at(TokenKind.Question)) {
      this.nextToken();
      questionToken = true;
    }
    const typeParameters = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
    if (this.at(TokenKind.OpenParen) || typeParameters.length > 0) {
      const parameters = this.parseParameters();
      let returnType: TypeNode | undefined;
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        returnType = this.parseType();
      }
      this.parseSemicolonOrComma();
      const sig: MethodSignature = { kind: SyntaxKind.MethodSignature, name, questionToken, typeParameters, parameters, returnType, start, end: returnType?.end ?? name.end };
      return sig;
    }
    let type: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      type = this.parseType();
    }
    this.parseSemicolonOrComma();
    const signature: PropertySignature = { kind: SyntaxKind.PropertySignature, name, questionToken, readonlyToken, type, start, end: type?.end ?? name.end };
    return signature;
  }

  private parseSemicolonOrComma(): void {
    if (this.at(TokenKind.Semicolon) || this.at(TokenKind.Comma)) this.nextToken();
  }

  // -- modules --------------------------------------------------------------

  private parseImportDeclaration(): ImportDeclaration {
    const start = this.parseExpected(TokenKind.ImportKeyword).start;
    let importClause: ImportClause | undefined;
    if (!this.at(TokenKind.StringLiteral)) {
      const clauseStart = this.token.start;
      let isTypeOnly = false;
      if (this.at(TokenKind.TypeKeyword) && !this.lookAhead(1).precededByLineBreak) {
        const next = this.lookAhead(1);
        if (this.isIdentifierLike(next) || next.kind === TokenKind.OpenBrace || next.kind === TokenKind.Asterisk) {
          this.nextToken();
          isTypeOnly = true;
        }
      }
      let name: Identifier | undefined;
      let namedBindings: NamedImports | NamespaceImport | undefined;
      if (this.isIdentifierLike(this.token)) {
        name = this.parseIdentifierName();
        if (this.at(TokenKind.Comma)) this.nextToken();
      }
      if (this.at(TokenKind.Asterisk)) {
        this.nextToken();
        this.parseExpected(TokenKind.AsKeyword);
        const nsName = this.parseIdentifier();
        namedBindings = { kind: SyntaxKind.NamespaceImport, name: nsName, start: nsName.start, end: nsName.end };
      } else if (this.at(TokenKind.OpenBrace)) {
        namedBindings = this.parseNamedImports();
      }
      importClause = { kind: SyntaxKind.ImportClause, name, namedBindings, isTypeOnly, start: clauseStart, end: namedBindings?.end ?? name?.end ?? clauseStart };
    }
    this.parseExpected(TokenKind.FromKeyword);
    const moduleToken = this.parseExpected(TokenKind.StringLiteral);
    const moduleSpecifier = this.stringLiteralFromToken(moduleToken);
    const attributes: { name: string; value: string }[] = [];
    if (this.at(TokenKind.WithKeyword) || (this.at(TokenKind.Identifier) && this.token.text === "assert")) {
      this.nextToken();
      this.parseExpected(TokenKind.OpenBrace);
      while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
        const key = this.parsePropertyName();
        this.parseExpected(TokenKind.Colon);
        const valueToken = this.parseExpected(TokenKind.StringLiteral);
        attributes.push({ name: propertyNameText(key), value: String(valueToken.value ?? "") });
        if (this.at(TokenKind.Comma)) this.nextToken();
        else break;
      }
      this.parseExpected(TokenKind.CloseBrace);
    }
    this.parseSemicolon();
    return { kind: SyntaxKind.ImportDeclaration, importClause, moduleSpecifier, attributes, start, end: moduleSpecifier.end };
  }

  private parseNamedImports(): NamedImports {
    const open = this.parseExpected(TokenKind.OpenBrace);
    const elements: ImportSpecifier[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const specStart = this.token.start;
      let isTypeOnly = false;
      if (this.at(TokenKind.TypeKeyword)) {
        this.nextToken();
        isTypeOnly = true;
      }
      const first = this.parseIdentifierName();
      let propertyName: Identifier | undefined;
      let name = first;
      if (this.at(TokenKind.AsKeyword)) {
        this.nextToken();
        propertyName = first;
        name = this.parseIdentifierName();
      }
      elements.push({ kind: SyntaxKind.ImportSpecifier, propertyName, name, isTypeOnly, start: specStart, end: name.end });
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.NamedImports, elements, start: open.start, end: close.end };
  }

  private parseExportDeclaration(): Statement {
    const start = this.parseExpected(TokenKind.ExportKeyword).start;
    if (this.at(TokenKind.Equals)) {
      this.nextToken();
      const expression = this.parseExpression();
      this.parseSemicolon();
      const assignment: ExportAssignment = { kind: SyntaxKind.ExportAssignment, isExportEquals: true, expression, start, end: expression.end };
      return assignment;
    }
    if (this.at(TokenKind.DefaultKeyword)) {
      this.nextToken();
      if (this.at(TokenKind.FunctionKeyword) || this.at(TokenKind.ClassKeyword) || this.at(TokenKind.InterfaceKeyword)) {
        return this.parseStatementWithModifiers([{ kind: SyntaxKind.Unknown, modifierKind: ModifierKind.Default, start, end: this.token.start }]);
      }
      const expression = this.parseAssignmentExpression();
      this.parseSemicolon();
      const assignment: ExportAssignment = { kind: SyntaxKind.ExportAssignment, isExportEquals: false, expression, start, end: expression.end };
      return assignment;
    }
    if (this.at(TokenKind.Asterisk)) {
      this.nextToken();
      let nsName: Identifier | undefined;
      if (this.at(TokenKind.AsKeyword)) {
        this.nextToken();
        nsName = this.parseIdentifierName();
      }
      let moduleSpecifier: StringLiteral | undefined;
      if (this.at(TokenKind.FromKeyword)) {
        this.nextToken();
        moduleSpecifier = this.stringLiteralFromToken(this.nextToken());
      }
      this.parseSemicolon();
      const decl: ExportDeclaration = {
        kind: SyntaxKind.ExportDeclaration,
        modifiers: [],
        exportClause: nsName ? { kind: SyntaxKind.NamespaceImport, name: nsName, start: nsName.start, end: nsName.end } : undefined,
        moduleSpecifier,
        isTypeOnly: false,
        start,
        end: moduleSpecifier?.end ?? nsName?.end ?? start,
      };
      return decl;
    }
    if (this.at(TokenKind.OpenBrace)) {
      const namedExports = this.parseNamedExports();
      let moduleSpecifier: StringLiteral | undefined;
      if (this.at(TokenKind.FromKeyword)) {
        this.nextToken();
        if (this.at(TokenKind.StringLiteral)) moduleSpecifier = this.stringLiteralFromToken(this.nextToken());
      }
      this.parseSemicolon();
      const decl: ExportDeclaration = { kind: SyntaxKind.ExportDeclaration, modifiers: [], exportClause: namedExports, moduleSpecifier, isTypeOnly: false, start, end: moduleSpecifier?.end ?? namedExports.end };
      return decl;
    }
    const modifiers = this.tryParseModifiers();
    modifiers.push({ kind: SyntaxKind.Unknown, modifierKind: ModifierKind.Export, start, end: this.token.start });
    return this.parseStatementWithModifiers(modifiers);
  }

  private parseNamedExports(): NamedExports {
    const open = this.parseExpected(TokenKind.OpenBrace);
    const elements: ExportSpecifier[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const specStart = this.token.start;
      let isTypeOnly = false;
      if (this.at(TokenKind.TypeKeyword)) {
        this.nextToken();
        isTypeOnly = true;
      }
      const first = this.parseIdentifierName();
      let propertyName: Identifier | undefined;
      let name = first;
      if (this.at(TokenKind.AsKeyword)) {
        this.nextToken();
        propertyName = first;
        name = this.parseIdentifierName();
      }
      elements.push({ kind: SyntaxKind.ExportSpecifier, propertyName, name, isTypeOnly, start: specStart, end: name.end });
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.NamedExports, elements, start: open.start, end: close.end };
  }

  private stringLiteralFromToken(token: Token): StringLiteral {
    return { kind: SyntaxKind.StringLiteral, text: token.text, raw: token.text, value: String(token.value ?? ""), start: token.start, end: token.end };
  }

  // -- state save / restore (for speculative parsing) -----------------------

  private saveState(): { index: number; tokens: Token[]; pos: number; consumed: number } {
    return { index: this.index, tokens: this.tokens.slice(), pos: this.scanner.position, consumed: this.consumed };
  }

  private restoreState(state: { index: number; tokens: Token[]; pos: number; consumed: number }): void {
    this.index = state.index;
    this.tokens = state.tokens;
    this.scanner.seek(state.pos);
    this.consumed = state.consumed;
  }

  // -- expressions ----------------------------------------------------------

  parseExpression(noIn = false): Expression {
    return this.parseAssignmentExpression(noIn);
  }

  private parseAssignmentExpression(noIn = false): Expression {
    const arrow = this.tryParseArrowFunction();
    if (arrow) return arrow;

    const start = this.token.start;
    const left = this.parseConditionalExpression(noIn);
    const token = this.token;
    const op = assignmentOperator(token.kind);
    if (op) {
      if (!isAssignmentTarget(left)) {
        this.error(DiagnosticCode.InvalidAssignmentTarget, "Invalid assignment target", token);
      }
      this.nextToken();
      const right = this.parseAssignmentExpression(noIn);
      const assignment = {
        kind: SyntaxKind.BinaryExpression,
        left,
        operator: op,
        right,
        operatorToken: token,
        start,
        end: right.end,
      } as unknown as BinaryExpression;
      return assignment;
    }
    return left;
  }

  private tryParseArrowFunction(): ArrowFunction | undefined {
    const start = this.token.start;
    let flags = NodeFlags.None;
    let async = false;
    let cursor = 0;
    if (this.at(TokenKind.AsyncKeyword) && !this.lookAhead(1).precededByLineBreak) {
      const next = this.lookAhead(1);
      if (next.kind === TokenKind.OpenParen || this.isIdentifierLike(next)) async = true;
    }
    if (async) cursor = 1;
    const afterTypeParams = this.countTypeParameterTokens(cursor);
    if (afterTypeParams >= 0) cursor = afterTypeParams;

    const first = this.lookAhead(cursor);
    if (first.kind === TokenKind.OpenParen) {
      const closeIndex = this.findMatchingParen(cursor);
      if (closeIndex < 0) return undefined;
      let arrowIndex = closeIndex + 1;
      if (this.lookAhead(arrowIndex).kind === TokenKind.Colon) {
        arrowIndex = this.skipTypeTokens(arrowIndex + 1);
        if (arrowIndex < 0) return undefined;
      }
      if (this.lookAhead(arrowIndex).kind !== TokenKind.EqualsGreaterThan) return undefined;
    } else if (this.isIdentifierLike(first)) {
      if (this.lookAhead(cursor + 1).kind !== TokenKind.EqualsGreaterThan) return undefined;
    } else {
      return undefined;
    }

    if (async) {
      this.nextToken();
      flags |= NodeFlags.Async;
    }
    const typeParams = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
    let parameters: Parameter[];
    if (this.at(TokenKind.OpenParen)) {
      parameters = this.parseParameters();
    } else {
      const paramStart = this.token.start;
      const name = this.parseIdentifierName();
      parameters = [{ kind: SyntaxKind.Parameter, name, modifiers: [], dotDotDotToken: false, questionToken: false, start: paramStart, end: name.end }];
    }
    let returnType: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      returnType = this.parseReturnType();
    }
    this.parseExpected(TokenKind.EqualsGreaterThan);
    const body: Block | Expression = this.at(TokenKind.OpenBrace) ? this.parseBlock() : this.parseAssignmentExpression();
    return { kind: SyntaxKind.ArrowFunction, typeParameters: typeParams, parameters, returnType, body, flags, start, end: body.end };
  }

  private countTypeParameterTokens(cursor: number): number {
    if (this.lookAhead(cursor).kind !== TokenKind.LessThan) return -1;
    let depth = 0;
    for (let i = cursor; ; i++) {
      const t = this.lookAhead(i);
      if (t.kind === TokenKind.EndOfFile) return -1;
      if (t.kind === TokenKind.LessThan) depth++;
      else if (t.kind === TokenKind.GreaterThan) {
        depth--;
        if (depth === 0) return i + 1;
      } else if (t.kind === TokenKind.GreaterThanGreaterThan || t.kind === TokenKind.GreaterThanGreaterThanGreaterThan) {
        depth -= t.kind === TokenKind.GreaterThanGreaterThan ? 2 : 3;
        if (depth <= 0) return i + 1;
      } else if (t.kind === TokenKind.Semicolon) {
        return -1;
      }
    }
  }

  private findMatchingParen(cursor: number): number {
    let depth = 0;
    for (let i = cursor; ; i++) {
      const t = this.lookAhead(i);
      if (t.kind === TokenKind.EndOfFile) return -1;
      if (t.kind === TokenKind.OpenParen) depth++;
      else if (t.kind === TokenKind.CloseParen) {
        depth--;
        if (depth === 0) return i;
      }
    }
  }

  private skipTypeTokens(startIndex: number): number {
    let depth = 0;
    for (let i = startIndex; ; i++) {
      const t = this.lookAhead(i);
      if (t.kind === TokenKind.EndOfFile) return -1;
      if (t.kind === TokenKind.OpenParen || t.kind === TokenKind.OpenBracket || t.kind === TokenKind.OpenBrace) depth++;
      else if (t.kind === TokenKind.CloseParen || t.kind === TokenKind.CloseBracket || t.kind === TokenKind.CloseBrace) depth--;
      else if (t.kind === TokenKind.EqualsGreaterThan && depth === 0) return i;
      else if (t.kind === TokenKind.Semicolon && depth === 0) return -1;
    }
  }

  private parseConditionalExpression(noIn: boolean): Expression {
    const start = this.token.start;
    const condition = this.parseBinaryExpression(0, noIn);
    if (this.at(TokenKind.Question)) {
      this.nextToken();
      const whenTrue = this.parseAssignmentExpression();
      this.parseExpected(TokenKind.Colon);
      const whenFalse = this.parseAssignmentExpression(noIn);
      const node: ConditionalExpression = { kind: SyntaxKind.ConditionalExpression, condition, whenTrue, whenFalse, start, end: whenFalse.end };
      return node;
    }
    return condition;
  }

  private parseBinaryExpression(minPrecedence: number, noIn: boolean): Expression {
    let left = this.parseUnaryExpression();
    for (;;) {
      const token = this.token;
      // `expr as Type` / `expr satisfies Type` bind at the relational level.
      if (token.kind === TokenKind.AsKeyword || token.kind === TokenKind.SatisfiesKeyword) {
        if (AS_PRECEDENCE < minPrecedence) break;
        this.nextToken();
        const type = this.parseType();
        left =
          token.kind === TokenKind.AsKeyword
            ? { kind: SyntaxKind.AsExpression, expression: left, type, start: left.start, end: type.end }
            : { kind: SyntaxKind.SatisfiesExpression, expression: left, type, start: left.start, end: type.end };
        continue;
      }
      if (token.kind === TokenKind.InKeyword && noIn) break;
      const op = binaryOperator(token.kind);
      if (!op) break;
      const precedence = binaryPrecedence(op);
      if (precedence < minPrecedence) break;
      const nextMin = op === BinaryOperator.Exponent ? precedence : precedence + 1;
      this.nextToken();
      const right = this.parseBinaryExpression(nextMin, noIn);
      const binary: BinaryExpression = { kind: SyntaxKind.BinaryExpression, left, operator: op, right, operatorToken: token, start: left.start, end: right.end };
      left = binary;
    }
    return left;
  }

  private parseUnaryExpression(): Expression {
    const token = this.token;
    const prefix = prefixUnaryOperator(token.kind);
    if (prefix) {
      this.nextToken();
      if (prefix === PrefixUnaryOperator.Await) {
        const expression = this.parseUnaryExpression();
        return { kind: SyntaxKind.AwaitExpression, expression, start: token.start, end: expression.end };
      }
      if (prefix === PrefixUnaryOperator.Delete) {
        const expression = this.parseUnaryExpression();
        return { kind: SyntaxKind.DeleteExpression, expression, start: token.start, end: expression.end };
      }
      if (prefix === PrefixUnaryOperator.Void) {
        const expression = this.parseUnaryExpression();
        return { kind: SyntaxKind.VoidExpression, expression, start: token.start, end: expression.end };
      }
      if (prefix === PrefixUnaryOperator.TypeOf) {
        const expression = this.parseUnaryExpression();
        return { kind: SyntaxKind.TypeOfExpression, expression, start: token.start, end: expression.end };
      }
      const operand = this.parseUnaryExpression();
      return { kind: SyntaxKind.PrefixUnaryExpression, operator: prefix, operand, start: token.start, end: operand.end };
    }
    if (token.kind === TokenKind.YieldKeyword) {
      this.nextToken();
      let delegate = false;
      let expression: Expression | undefined;
      if (this.at(TokenKind.Asterisk)) {
        this.nextToken();
        delegate = true;
      }
      if (!this.canInsertSemicolon() && !this.at(TokenKind.Semicolon) && !this.at(TokenKind.CloseParen)) {
        expression = this.parseAssignmentExpression();
      }
      return { kind: SyntaxKind.YieldExpression, expression, delegate, start: token.start, end: expression?.end ?? token.end };
    }
    if (token.kind === TokenKind.LessThan) {
      const assertion = this.tryParse<Expression>(() => {
        const type = this.parseType();
        const expression = this.parseUnaryExpression();
        return { kind: SyntaxKind.AsExpression, expression, type, start: token.start, end: expression.end };
      });
      if (assertion) return assertion;
    }
    return this.parsePostfixExpression();
  }

  private parsePostfixExpression(): Expression {
    const start = this.token.start;
    const expression = this.parseLeftHandSideExpression();
    const token = this.token;
    if ((token.kind === TokenKind.PlusPlus || token.kind === TokenKind.MinusMinus) && !token.precededByLineBreak) {
      this.nextToken();
      const operator = token.kind === TokenKind.PlusPlus ? PostfixUnaryOperator.PlusPlus : PostfixUnaryOperator.MinusMinus;
      return { kind: SyntaxKind.PostfixUnaryExpression, operator, operand: expression, start, end: token.end };
    }
    return expression;
  }

  private parseLeftHandSideExpression(): Expression {
    const expression = this.at(TokenKind.NewKeyword) ? this.parseNewExpression() : this.parsePrimaryExpression();
    return this.parseCallAndMemberTail(expression);
  }

  private parseNewExpression(): NewExpression {
    const start = this.parseExpected(TokenKind.NewKeyword).start;
    const callee = this.parseMemberOnlyExpression();
    const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
    const args = this.at(TokenKind.OpenParen) ? this.parseArguments() : [];
    return { kind: SyntaxKind.NewExpression, expression: callee, typeArguments, arguments: args, start, end: args[args.length - 1]?.end ?? callee.end };
  }

  private parseMemberOnlyExpression(): Expression {
    let expression: Expression = this.parsePrimaryExpression();
    for (;;) {
      if (this.at(TokenKind.Dot) || this.at(TokenKind.QuestionDot)) {
        expression = this.parsePropertyAccess(expression);
      } else if (this.at(TokenKind.OpenBracket)) {
        this.nextToken();
        const argument = this.parseExpression();
        const close = this.parseExpected(TokenKind.CloseBracket);
        expression = { kind: SyntaxKind.ElementAccessExpression, expression, argumentExpression: argument, optional: false, start: expression.start, end: close.end };
      } else {
        break;
      }
    }
    return expression;
  }

  private parsePrimaryExpression(): Expression {
    const token = this.token;
    switch (token.kind) {
      case TokenKind.NumericLiteral: {
        this.nextToken();
        const node: NumericLiteral = { kind: SyntaxKind.NumericLiteral, text: token.text, value: Number(token.value ?? 0), start: token.start, end: token.end };
        return node;
      }
      case TokenKind.BigIntLiteral:
        this.nextToken();
        return { kind: SyntaxKind.BigIntLiteral, text: token.text, value: BigInt(token.value ?? 0n), start: token.start, end: token.end };
      case TokenKind.StringLiteral:
        this.nextToken();
        return this.stringLiteralFromToken(token);
      case TokenKind.TrueKeyword:
        this.nextToken();
        return { kind: SyntaxKind.TrueKeyword, value: true, start: token.start, end: token.end } satisfies BooleanLiteral;
      case TokenKind.FalseKeyword:
        this.nextToken();
        return { kind: SyntaxKind.FalseKeyword, value: false, start: token.start, end: token.end } satisfies BooleanLiteral;
      case TokenKind.NullKeyword:
        this.nextToken();
        return { kind: SyntaxKind.NullKeyword, start: token.start, end: token.end };
      case TokenKind.UndefinedKeyword:
        this.nextToken();
        return { kind: SyntaxKind.UndefinedKeyword, start: token.start, end: token.end };
      case TokenKind.ThisKeyword:
        this.nextToken();
        return { kind: SyntaxKind.ThisKeyword, start: token.start, end: token.end };
      case TokenKind.OpenParen: {
        this.nextToken();
        const expression = this.parseExpression();
        const close = this.parseExpected(TokenKind.CloseParen);
        const paren: ParenthesizedExpression = { kind: SyntaxKind.ParenthesizedExpression, expression, start: token.start, end: close.end };
        return paren;
      }
      case TokenKind.OpenBracket:
        return this.parseArrayLiteral();
      case TokenKind.OpenBrace:
        return this.parseObjectLiteral();
      case TokenKind.FunctionKeyword:
        return this.parseFunctionExpression();
      case TokenKind.ClassKeyword:
        return this.parseClassExpression();
      case TokenKind.AsyncKeyword:
        if (this.atAhead(1, TokenKind.FunctionKeyword) && !this.lookAhead(1).precededByLineBreak) return this.parseFunctionExpression();
        break;
      case TokenKind.Backtick:
      case TokenKind.TemplateHead:
      case TokenKind.NoSubstitutionTemplateLiteral:
        return this.parseTemplateLiteral();
      case TokenKind.Slash:
        return this.parseRegularExpression();
      default:
        break;
    }
    if (this.isIdentifierLike(token)) return this.parseIdentifierName();
    this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${token.text || token.kind}'`);
    if (token.kind !== TokenKind.EndOfFile) this.nextToken();
    return { kind: SyntaxKind.Identifier, text: "<error>", start: token.start, end: token.end };
  }

  private parseCallAndMemberTail(base: Expression): Expression {
    let expression = base;
    for (;;) {
      const token = this.token;
      if (token.kind === TokenKind.Dot || token.kind === TokenKind.QuestionDot) {
        expression = this.parsePropertyAccess(expression);
        continue;
      }
      if (token.kind === TokenKind.OpenBracket) {
        this.nextToken();
        const argument = this.parseExpression();
        const close = this.parseExpected(TokenKind.CloseBracket);
        expression = { kind: SyntaxKind.ElementAccessExpression, expression, argumentExpression: argument, optional: false, start: expression.start, end: close.end };
        continue;
      }
      if (token.kind === TokenKind.OpenParen) {
        const args = this.parseArguments();
        const call: CallExpression = { kind: SyntaxKind.CallExpression, expression, typeArguments: [], arguments: args, optional: false, start: expression.start, end: this.previousEnd(args, expression.end) };
        expression = call;
        continue;
      }
      if (token.kind === TokenKind.LessThan) {
        const call = this.tryParse<Expression>(() => {
          const typeArguments = this.parseTypeArguments();
          if (!this.at(TokenKind.OpenParen)) throw new SpeculationError();
          const args = this.parseArguments();
          return { kind: SyntaxKind.CallExpression, expression, typeArguments, arguments: args, optional: false, start: expression.start, end: this.previousEnd(args, this.token.start) };
        });
        if (call) {
          expression = call;
          continue;
        }
      }
      if (this.at(TokenKind.Backtick) || this.at(TokenKind.TemplateHead) || this.at(TokenKind.NoSubstitutionTemplateLiteral)) {
        const template = this.parseTemplateLiteral();
        expression = { kind: SyntaxKind.TaggedTemplateExpression, tag: expression, template, start: expression.start, end: template.end };
        continue;
      }
      return expression;
    }
  }

  private previousEnd(args: readonly Expression[], fallback: number): number {
    return args[args.length - 1]?.end ?? fallback;
  }

  private parsePropertyAccess(expression: Expression): Expression {
    const token = this.nextToken();
    const optional = token.kind === TokenKind.QuestionDot;
    if (optional && this.at(TokenKind.OpenParen)) {
      const args = this.parseArguments();
      return { kind: SyntaxKind.CallExpression, expression, typeArguments: [], arguments: args, optional: true, start: expression.start, end: this.previousEnd(args, token.start) };
    }
    if (optional && this.at(TokenKind.OpenBracket)) {
      this.nextToken();
      const argument = this.parseExpression();
      const close = this.parseExpected(TokenKind.CloseBracket);
      return { kind: SyntaxKind.ElementAccessExpression, expression, argumentExpression: argument, optional: true, start: expression.start, end: close.end };
    }
    const name = this.parseIdentifierName();
    return { kind: SyntaxKind.PropertyAccessExpression, expression, name, optional, start: expression.start, end: name.end };
  }

  private parseArguments(): Expression[] {
    this.parseExpected(TokenKind.OpenParen);
    const args: Expression[] = [];
    while (!this.at(TokenKind.CloseParen) && !this.at(TokenKind.EndOfFile)) {
      if (this.at(TokenKind.DotDotDot)) {
        const start = this.nextToken().start;
        const expression = this.parseAssignmentExpression();
        args.push({ kind: SyntaxKind.SpreadElement, expression, start, end: expression.end });
      } else {
        args.push(this.parseAssignmentExpression());
      }
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseExpected(TokenKind.CloseParen);
    return args;
  }

  private parseArrayLiteral(): ArrayLiteralExpression {
    const open = this.parseExpected(TokenKind.OpenBracket);
    const elements: Expression[] = [];
    while (!this.at(TokenKind.CloseBracket) && !this.at(TokenKind.EndOfFile)) {
      if (this.at(TokenKind.Comma)) {
        // Elision: represent as an undefined literal hole.
        elements.push({ kind: SyntaxKind.UndefinedKeyword, start: this.token.start, end: this.token.start });
        this.nextToken();
        continue;
      }
      if (this.at(TokenKind.DotDotDot)) {
        const start = this.nextToken().start;
        const expression = this.parseAssignmentExpression();
        elements.push({ kind: SyntaxKind.SpreadElement, expression, start, end: expression.end });
      } else {
        elements.push(this.parseAssignmentExpression());
      }
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBracket);
    return { kind: SyntaxKind.ArrayLiteralExpression, elements, start: open.start, end: close.end };
  }

  private parseObjectLiteral(): ObjectLiteralExpression {
    const open = this.parseExpected(TokenKind.OpenBrace);
    const properties: ObjectLiteralElementLike[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      if (this.at(TokenKind.DotDotDot)) {
        const start = this.nextToken().start;
        const expression = this.parseAssignmentExpression();
        properties.push({ kind: SyntaxKind.SpreadElement, expression, start, end: expression.end });
      } else {
        const start = this.token.start;
        const name = this.parsePropertyName();
        if (this.at(TokenKind.Colon)) {
          this.nextToken();
          const initializer = this.parseAssignmentExpression();
          properties.push({ kind: SyntaxKind.PropertyAssignment, name, initializer, start, end: initializer.end });
        } else if (this.at(TokenKind.OpenParen) || this.at(TokenKind.LessThan)) {
          const typeParameters = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
          const parameters = this.parseParameters();
          let returnType: TypeNode | undefined;
          if (this.at(TokenKind.Colon)) {
            this.nextToken();
            returnType = this.parseReturnType();
          }
          const body = this.parseBlock();
          const fn: FunctionExpression = { kind: SyntaxKind.FunctionExpression, typeParameters, parameters, returnType, body, flags: NodeFlags.None, start, end: body.end };
          properties.push({ kind: SyntaxKind.PropertyAssignment, name, initializer: fn, start, end: body.end });
        } else if (name.kind === SyntaxKind.Identifier) {
          const shorthand: ShorthandPropertyAssignment = { kind: SyntaxKind.ShorthandPropertyAssignment, name, start, end: name.end };
          properties.push(shorthand);
        } else {
          this.error(DiagnosticCode.UnexpectedToken, "Invalid object literal member");
          this.nextToken();
        }
      }
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.ObjectLiteralExpression, properties, start: open.start, end: close.end };
  }

  private parseFunctionExpression(): FunctionExpression {
    const start = this.parseExpected(TokenKind.FunctionKeyword).start;
    let flags = NodeFlags.None;
    if (this.at(TokenKind.Asterisk)) {
      this.nextToken();
      flags |= NodeFlags.Generator;
    }
    let name: Identifier | undefined;
    if (this.at(TokenKind.Identifier)) name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const parameters = this.parseParameters();
    let returnType: TypeNode | undefined;
    if (this.at(TokenKind.Colon)) {
      this.nextToken();
      returnType = this.parseReturnType();
    }
    const body = this.parseBlock();
    return { kind: SyntaxKind.FunctionExpression, name, typeParameters, parameters, returnType, body, flags, start, end: body.end };
  }

  private parseClassExpression(): import("../ast/nodes.js").ClassExpression {
    const start = this.parseExpected(TokenKind.ClassKeyword).start;
    let name: Identifier | undefined;
    if (this.at(TokenKind.Identifier)) name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseClassMembers();
    return { kind: SyntaxKind.ClassExpression, name, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? start };
  }

  private parseTemplateLiteral(): TemplateLiteral {
    const startToken = this.token;
    if (startToken.kind === TokenKind.NoSubstitutionTemplateLiteral) {
      this.nextToken();
      return { kind: SyntaxKind.NoSubstitutionTemplateLiteral, text: startToken.text, value: String(startToken.value ?? ""), start: startToken.start, end: startToken.end } as unknown as TemplateLiteral;
    }
    if (startToken.kind === TokenKind.Backtick) {
      // Re-scan from the backtick so the scanner produces a TemplateHead.
      this.scanner.seek(startToken.start);
      this.tokens = [this.scanner.nextToken()];
      this.index = 0;
      return this.parseTemplateLiteral();
    }    const head = String(startToken.value ?? "");
    this.nextToken();
    const spans: TemplateSpan[] = [];
    for (;;) {
      const expression = this.parseExpression();
      if (!this.at(TokenKind.CloseBrace)) {
        this.error(DiagnosticCode.UnterminatedTemplate, "Expected '}' to close template substitution");
        return { kind: SyntaxKind.TemplateLiteral, head, spans, start: startToken.start, end: this.token.end };
      }
      // The `}` terminates a substitution; re-read it as template text without
      // first scanning the following (template) characters as normal tokens.
      const closeStart = this.token.start;
      this.tokens = [this.scanner.continueTemplate(closeStart)];
      this.index = 0;
      const literalToken = this.token;
      if (literalToken.kind === TokenKind.NoSubstitutionTemplateLiteral) {
        this.nextToken();
        spans.push({ kind: SyntaxKind.TemplateSpan, expression, literal: String(literalToken.value ?? ""), isTail: true, start: expression.start, end: literalToken.end });
        return { kind: SyntaxKind.TemplateLiteral, head, spans, start: startToken.start, end: literalToken.end };
      }
      if (literalToken.kind === TokenKind.TemplateHead) {
        this.nextToken();
        spans.push({ kind: SyntaxKind.TemplateSpan, expression, literal: String(literalToken.value ?? ""), isTail: false, start: expression.start, end: literalToken.end });
        continue;
      }
      this.error(DiagnosticCode.UnterminatedTemplate, "Unterminated template literal", literalToken);
      return { kind: SyntaxKind.TemplateLiteral, head, spans, start: startToken.start, end: literalToken.end };
    }
  }

  private parseRegularExpression(): RegularExpressionLiteral {
    const token = this.token;
    const text = this.source.text;
    let i = token.start + 1;
    let inClass = false;
    let pattern = "";
    for (; i < text.length; i++) {
      const ch = text[i]!;
      if (ch === "\\") {
        pattern += ch + (text[i + 1] ?? "");
        i++;
        continue;
      }
      if (ch === "[") inClass = true;
      else if (ch === "]") inClass = false;
      else if (ch === "/" && !inClass) break;
      else if (ch === "\n") break;
      pattern += ch;
    }
    if (text[i] !== "/") {
      this.error(DiagnosticCode.InvalidCharacter, "Unterminated regular expression");
      this.nextToken();
      return { kind: SyntaxKind.RegularExpressionLiteral, text: token.text, pattern: "", flags: "", start: token.start, end: token.end };
    }
    i++;
    const flagsStart = i;
    while (i < text.length && /[a-z]/i.test(text[i]!)) i++;
    const flags = text.slice(flagsStart, i);
    this.scanner.seek(i);
    this.tokens = [this.scanner.nextToken()];
    this.index = 0;
    return { kind: SyntaxKind.RegularExpressionLiteral, text: text.slice(token.start, i), pattern, flags, start: token.start, end: i };
  }

  private parseTypeArguments(): TypeNode[] {
    this.parseExpected(TokenKind.LessThan);
    const types: TypeNode[] = [];
    while (!this.at(TokenKind.GreaterThan) && !this.at(TokenKind.EndOfFile)) {
      types.push(this.parseType());
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseGreaterThan();
    return types;
  }

  // -- types ----------------------------------------------------------------

  parseType(): TypeNode {
    const start = this.token.start;
    const checkType = this.parseUnionType();
    if (this.at(TokenKind.ExtendsKeyword)) {
      this.nextToken();
      const extendsType = this.parseUnionType();
      this.parseExpected(TokenKind.Question);
      const trueType = this.parseType();
      this.parseExpected(TokenKind.Colon);
      const falseType = this.parseType();
      return { kind: SyntaxKind.ConditionalType, checkType, extendsType, trueType, falseType, start, end: falseType.end };
    }
    return checkType;
  }

  private parseUnionType(): TypeNode {
    const start = this.token.start;
    const leading = this.at(TokenKind.Bar);
    if (leading) this.nextToken();
    const first = this.parseIntersectionType();
    if (!this.at(TokenKind.Bar)) return first;
    const types: TypeNode[] = [first];
    while (this.at(TokenKind.Bar)) {
      this.nextToken();
      types.push(this.parseIntersectionType());
    }
    return { kind: SyntaxKind.UnionType, types, start, end: types[types.length - 1]!.end };
  }

  private parseIntersectionType(): TypeNode {
    const start = this.token.start;
    const leading = this.at(TokenKind.Ampersand);
    if (leading) this.nextToken();
    const first = this.parseTypeOperatorOrHigher();
    if (!this.at(TokenKind.Ampersand)) return first;
    const types: TypeNode[] = [first];
    while (this.at(TokenKind.Ampersand)) {
      this.nextToken();
      types.push(this.parseTypeOperatorOrHigher());
    }
    return { kind: SyntaxKind.IntersectionType, types, start, end: types[types.length - 1]!.end };
  }

  private parseTypeOperatorOrHigher(): TypeNode {
    const start = this.token.start;
    if (this.at(TokenKind.KeyOfKeyword)) {
      this.nextToken();
      const type = this.parseTypeOperatorOrHigher();
      return { kind: SyntaxKind.TypeOperator, operator: TypeOperator.KeyOf, type, start, end: type.end };
    }
    if (this.at(TokenKind.UniqueKeyword)) {
      this.nextToken();
      const type = this.parseTypeOperatorOrHigher();
      return { kind: SyntaxKind.TypeOperator, operator: TypeOperator.Unique, type, start, end: type.end };
    }
    if (this.at(TokenKind.ReadonlyKeyword)) {
      this.nextToken();
      const type = this.parseTypeOperatorOrHigher();
      return { kind: SyntaxKind.TypeOperator, operator: TypeOperator.Readonly, type, start, end: type.end };
    }
    if (this.at(TokenKind.InferKeyword)) {
      this.nextToken();
      const name = this.parseIdentifier();
      let constraint: TypeNode | undefined;
      if (this.at(TokenKind.ExtendsKeyword)) {
        this.nextToken();
        constraint = this.parseType();
      }
      const typeParameter: TypeParameterDeclaration = { kind: SyntaxKind.TypeParameterDeclaration, name, constraint, start: name.start, end: constraint?.end ?? name.end };
      return { kind: SyntaxKind.InferType, typeParameter, start, end: typeParameter.end };
    }
    return this.parsePostfixType();
  }

  private parsePostfixType(): TypeNode {
    const start = this.token.start;
    let type = this.parsePrimaryType();
    for (;;) {
      if (this.at(TokenKind.OpenBracket)) {
        this.nextToken();
        if (this.at(TokenKind.CloseBracket)) {
          const close = this.nextToken();
          type = { kind: SyntaxKind.ArrayType, elementType: type, start, end: close.end };
        } else {
          const indexType = this.parseType();
          const close = this.parseExpected(TokenKind.CloseBracket);
          type = { kind: SyntaxKind.IndexedAccessType, objectType: type, indexType, start, end: close.end };
        }
        continue;
      }
      if (this.at(TokenKind.Question) && this.atAhead(1, TokenKind.Colon)) {
        // Optional tuple member, handled inside tuples only.
      }
      return type;
    }
  }

  private parsePrimaryType(): TypeNode {
    const token = this.token;
    const start = token.start;
    switch (token.kind) {
      case TokenKind.AnyKeyword:
        this.nextToken();
        return { kind: SyntaxKind.AnyKeywordType, start, end: token.end };
      case TokenKind.UnknownKeyword:
        this.nextToken();
        return { kind: SyntaxKind.UnknownKeywordType, start, end: token.end };
      case TokenKind.NumberKeyword:
        this.nextToken();
        return { kind: SyntaxKind.NumberKeywordType, start, end: token.end };
      case TokenKind.BigIntKeyword:
        this.nextToken();
        return { kind: SyntaxKind.BigIntKeywordType, start, end: token.end };
      case TokenKind.StringKeyword:
        this.nextToken();
        return { kind: SyntaxKind.StringKeywordType, start, end: token.end };
      case TokenKind.BooleanKeyword:
        this.nextToken();
        return { kind: SyntaxKind.BooleanKeywordType, start, end: token.end };
      case TokenKind.SymbolKeyword:
        this.nextToken();
        return { kind: SyntaxKind.SymbolKeywordType, start, end: token.end };
      case TokenKind.VoidKeyword:
        this.nextToken();
        return { kind: SyntaxKind.VoidKeywordType, start, end: token.end };
      case TokenKind.UndefinedKeyword:
        this.nextToken();
        return { kind: SyntaxKind.UndefinedKeywordType, start, end: token.end };
      case TokenKind.NullKeyword:
        this.nextToken();
        return { kind: SyntaxKind.NullKeywordType, start, end: token.end };
      case TokenKind.NeverKeyword:
        this.nextToken();
        return { kind: SyntaxKind.NeverKeywordType, start, end: token.end };
      case TokenKind.ObjectKeyword:
        this.nextToken();
        return { kind: SyntaxKind.ObjectKeywordType, start, end: token.end };
      case TokenKind.ThisKeyword:
        this.nextToken();
        return { kind: SyntaxKind.ThisKeywordType, start, end: token.end };
      case TokenKind.TypeOfKeyword: {
        this.nextToken();
        const exprName = this.parseEntityName();
        const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
        return { kind: SyntaxKind.TypeQuery, exprName, typeArguments, start, end: this.token.start };
      }
      case TokenKind.OpenBrace:
        return this.parseTypeLiteral();
      case TokenKind.OpenBracket:
        return this.parseTupleType();
      case TokenKind.OpenParen: {
        const closeIndex = this.findMatchingParen(0);
        if (closeIndex >= 0 && this.lookAhead(closeIndex + 1).kind === TokenKind.EqualsGreaterThan) {
          return this.parseFunctionType();
        }
        this.nextToken();
        const type = this.parseType();
        const close = this.parseExpected(TokenKind.CloseParen);
        return { kind: SyntaxKind.ParenthesizedType, type, start, end: close.end };
      }
      case TokenKind.LessThan:
        return this.parseFunctionType();
      case TokenKind.NewKeyword: {
        this.nextToken();
        return this.parseFunctionType(start);
      }
      case TokenKind.StringLiteral: {
        this.nextToken();
        const literal = this.stringLiteralFromToken(token);
        return { kind: SyntaxKind.LiteralType, literal, start, end: token.end };
      }
      case TokenKind.NumericLiteral: {
        this.nextToken();
        const literal: NumericLiteral = { kind: SyntaxKind.NumericLiteral, text: token.text, value: Number(token.value ?? 0), start, end: token.end };
        return { kind: SyntaxKind.LiteralType, literal, start, end: token.end };
      }
      case TokenKind.TrueKeyword: {
        this.nextToken();
        const literal: BooleanLiteral = { kind: SyntaxKind.TrueKeyword, value: true, start, end: token.end };
        return { kind: SyntaxKind.LiteralType, literal, start, end: token.end };
      }
      case TokenKind.FalseKeyword: {
        this.nextToken();
        const literal: BooleanLiteral = { kind: SyntaxKind.FalseKeyword, value: false, start, end: token.end };
        return { kind: SyntaxKind.LiteralType, literal, start, end: token.end };
      }
      case TokenKind.Minus: {
        this.nextToken();
        const num = this.parseExpected(TokenKind.NumericLiteral);
        const literal: NumericLiteral = { kind: SyntaxKind.NumericLiteral, text: `-${num.text}`, value: -Number(num.value ?? 0), start, end: num.end };
        return { kind: SyntaxKind.LiteralType, literal, start, end: num.end };
      }
      case TokenKind.ImportKeyword: {
        // import("...").T - parse loosely and degrade to `any`.
        this.nextToken();
        if (this.at(TokenKind.OpenParen)) {
          this.nextToken();
          if (this.at(TokenKind.StringLiteral)) this.nextToken();
          this.parseExpected(TokenKind.CloseParen);
        }
        while (this.at(TokenKind.Dot)) {
          this.nextToken();
          this.parseIdentifierName();
        }
        const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
        return { kind: SyntaxKind.AnyKeywordType, start, end: this.token.start, typeArguments } as unknown as TypeNode;
      }
      default:
        break;
    }
    if (this.isIdentifierLike(token)) {
      const typeName = this.parseEntityName();
      const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
      return { kind: SyntaxKind.TypeReference, typeName, typeArguments, start, end: this.previousTypeEnd(typeArguments, typeName.end) };
    }
    this.error(DiagnosticCode.InvalidTypeSyntax, `Expected a type but found '${token.text || token.kind}'`);
    if (token.kind !== TokenKind.EndOfFile) this.nextToken();
    return { kind: SyntaxKind.AnyKeywordType, start, end: token.end };
  }

  private previousTypeEnd(typeArguments: readonly TypeNode[], fallback: number): number {
    return typeArguments[typeArguments.length - 1]?.end ?? fallback;
  }

  private parseEntityName(): Identifier | import("../ast/nodes.js").QualifiedName {
    let left: Identifier | import("../ast/nodes.js").QualifiedName = this.parseIdentifierName();
    while (this.at(TokenKind.Dot)) {
      this.nextToken();
      const right = this.parseIdentifierName();
      left = { kind: SyntaxKind.QualifiedName, left, right, start: left.start, end: right.end };
    }
    return left;
  }

  private parseFunctionType(explicitStart?: number): import("../ast/nodes.js").FunctionTypeNode {
    const start = explicitStart ?? this.token.start;
    const typeParameters = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
    const parameters = this.parseParameters();
    this.parseExpected(TokenKind.EqualsGreaterThan);
    const returnType = this.parseReturnType();
    return { kind: SyntaxKind.FunctionType, typeParameters, parameters, returnType, start, end: returnType.end };
  }

  private parseTupleType(): import("../ast/nodes.js").TupleTypeNode {
    const open = this.parseExpected(TokenKind.OpenBracket);
    const elements: TypeNode[] = [];
    while (!this.at(TokenKind.CloseBracket) && !this.at(TokenKind.EndOfFile)) {
      let element: TypeNode;
      if (this.at(TokenKind.DotDotDot)) {
        const restStart = this.nextToken().start;
        const type = this.parseType();
        element = { kind: SyntaxKind.RestType, type, start: restStart, end: type.end };
      } else {
        element = this.parseType();
      }
      if (this.at(TokenKind.Question)) {
        const end = this.nextToken().end;
        element = { kind: SyntaxKind.OptionalType, type: element, start: element.start, end };
      }
      elements.push(element);
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBracket);
    return { kind: SyntaxKind.TupleType, elements, start: open.start, end: close.end };
  }

  private parseTypeLiteral(): import("../ast/nodes.js").TypeLiteralNode | import("../ast/nodes.js").MappedTypeNode {
    const start = this.token.start;
    // Detect mapped type: { [K in T]: U }
    let cursor = 0;
    if (this.atAhead(1, TokenKind.OpenBracket)) {
      cursor = 1;
      if (this.atAhead(2, TokenKind.Identifier)) {
        let i = 3;
        if (this.lookAhead(i).kind === TokenKind.InKeyword) {
          const open = this.parseExpected(TokenKind.OpenBrace);
          this.parseExpected(TokenKind.OpenBracket);
          const name = this.parseIdentifier();
          this.parseExpected(TokenKind.InKeyword);
          let constraint = this.parseType();
          this.parseExpected(TokenKind.CloseBracket);
          let questionToken: boolean | "+" | "-" = false;
          if (this.at(TokenKind.Question)) {
            this.nextToken();
            questionToken = true;
          } else if (this.at(TokenKind.Plus)) {
            this.nextToken();
            this.nextToken();
            questionToken = "+";
          } else if (this.at(TokenKind.Minus)) {
            this.nextToken();
            this.nextToken();
            questionToken = "-";
          }
          let type: TypeNode | undefined;
          if (this.at(TokenKind.Colon)) {
            this.nextToken();
            type = this.parseType();
          }
          this.parseSemicolonOrComma();
          const close = this.parseExpected(TokenKind.CloseBrace);
          const typeParameter: TypeParameterDeclaration = { kind: SyntaxKind.TypeParameterDeclaration, name, constraint, start: name.start, end: constraint.end };
          return { kind: SyntaxKind.MappedType, readonlyToken: false, typeParameter, questionToken, type, start: open.start, end: close.end };
        }
      }
    }
    void cursor;
    const members = this.parseTypeMembers();
    return { kind: SyntaxKind.TypeLiteral, members, start, end: this.token.start };
  }
}

function expressionToPropertyName(expr: Expression): PropertyName | undefined {
  switch (expr.kind) {
    case SyntaxKind.StringLiteral:
      return expr;
    case SyntaxKind.NumericLiteral:
      return expr;
    case SyntaxKind.Identifier:
      return expr;
    default:
      return undefined;
  }
}

function propertyNameText(name: PropertyName): string {
  switch (name.kind) {
    case SyntaxKind.Identifier:
    case SyntaxKind.PrivateIdentifier:
      return name.text;
    case SyntaxKind.StringLiteral:
      return name.value;
    case SyntaxKind.NumericLiteral:
      return name.text;
    default:
      return "";
  }
}

function isAssignmentTarget(expr: Expression): boolean {
  return (
    expr.kind === SyntaxKind.Identifier ||
    expr.kind === SyntaxKind.PropertyAccessExpression ||
    expr.kind === SyntaxKind.ElementAccessExpression ||
    expr.kind === SyntaxKind.ParenthesizedExpression
  );
}

function assignmentOperator(kind: TokenKind): AssignmentOperator | undefined {
  switch (kind) {
    case TokenKind.Equals:
      return AssignmentOperator.Assign;
    case TokenKind.PlusEquals:
      return AssignmentOperator.AddAssign;
    case TokenKind.MinusEquals:
      return AssignmentOperator.SubtractAssign;
    case TokenKind.AsteriskEquals:
      return AssignmentOperator.MultiplyAssign;
    case TokenKind.SlashEquals:
      return AssignmentOperator.DivideAssign;
    case TokenKind.PercentEquals:
      return AssignmentOperator.RemainderAssign;
    case TokenKind.AsteriskAsteriskEquals:
      return AssignmentOperator.ExponentAssign;
    case TokenKind.LessThanLessThanEquals:
      return AssignmentOperator.LessThanLessThanAssign;
    case TokenKind.GreaterThanGreaterThanEquals:
      return AssignmentOperator.GreaterThanGreaterThanAssign;
    case TokenKind.GreaterThanGreaterThanGreaterThanEquals:
      return AssignmentOperator.GreaterThanGreaterThanGreaterThanAssign;
    case TokenKind.AmpersandEquals:
      return AssignmentOperator.AmpersandAssign;
    case TokenKind.BarEquals:
      return AssignmentOperator.BarAssign;
    case TokenKind.CaretEquals:
      return AssignmentOperator.CaretAssign;
    case TokenKind.AmpersandAmpersandEquals:
      return AssignmentOperator.AmpersandAmpersandAssign;
    case TokenKind.BarBarEquals:
      return AssignmentOperator.BarBarAssign;
    case TokenKind.QuestionQuestionEquals:
      return AssignmentOperator.QuestionQuestionAssign;
    default:
      return undefined;
  }
}

/** `as` / `satisfies` share the relational precedence level. */
const AS_PRECEDENCE = 8;

function binaryOperator(kind: TokenKind): BinaryOperator | undefined {
  switch (kind) {
    case TokenKind.Plus:
      return BinaryOperator.Add;
    case TokenKind.Minus:
      return BinaryOperator.Subtract;
    case TokenKind.Asterisk:
      return BinaryOperator.Multiply;
    case TokenKind.Slash:
      return BinaryOperator.Divide;
    case TokenKind.Percent:
      return BinaryOperator.Remainder;
    case TokenKind.AsteriskAsterisk:
      return BinaryOperator.Exponent;
    case TokenKind.LessThan:
      return BinaryOperator.LessThan;
    case TokenKind.LessThanEquals:
      return BinaryOperator.LessThanEquals;
    case TokenKind.GreaterThan:
      return BinaryOperator.GreaterThan;
    case TokenKind.GreaterThanEquals:
      return BinaryOperator.GreaterThanEquals;
    case TokenKind.EqualsEquals:
      return BinaryOperator.EqualsEquals;
    case TokenKind.ExclamationEquals:
      return BinaryOperator.ExclamationEquals;
    case TokenKind.EqualsEqualsEquals:
      return BinaryOperator.EqualsEqualsEquals;
    case TokenKind.ExclamationEqualsEquals:
      return BinaryOperator.ExclamationEqualsEquals;
    case TokenKind.AmpersandAmpersand:
      return BinaryOperator.AmpersandAmpersand;
    case TokenKind.BarBar:
      return BinaryOperator.BarBar;
    case TokenKind.QuestionQuestion:
      return BinaryOperator.QuestionQuestion;
    case TokenKind.Ampersand:
      return BinaryOperator.Ampersand;
    case TokenKind.Bar:
      return BinaryOperator.Bar;
    case TokenKind.Caret:
      return BinaryOperator.Caret;
    case TokenKind.LessThanLessThan:
      return BinaryOperator.LessThanLessThan;
    case TokenKind.GreaterThanGreaterThan:
      return BinaryOperator.GreaterThanGreaterThan;
    case TokenKind.GreaterThanGreaterThanGreaterThan:
      return BinaryOperator.GreaterThanGreaterThanGreaterThan;
    case TokenKind.InKeyword:
      return BinaryOperator.In;
    case TokenKind.InstanceOfKeyword:
      return BinaryOperator.InstanceOf;
    default:
      return undefined;
  }
}

function binaryPrecedence(op: BinaryOperator): number {
  switch (op) {
    case BinaryOperator.QuestionQuestion:
      return 1;
    case BinaryOperator.BarBar:
      return 2;
    case BinaryOperator.AmpersandAmpersand:
      return 3;
    case BinaryOperator.Bar:
      return 4;
    case BinaryOperator.Caret:
      return 5;
    case BinaryOperator.Ampersand:
      return 6;
    case BinaryOperator.EqualsEquals:
    case BinaryOperator.ExclamationEquals:
    case BinaryOperator.EqualsEqualsEquals:
    case BinaryOperator.ExclamationEqualsEquals:
      return 7;
    case BinaryOperator.LessThan:
    case BinaryOperator.LessThanEquals:
    case BinaryOperator.GreaterThan:
    case BinaryOperator.GreaterThanEquals:
    case BinaryOperator.In:
    case BinaryOperator.InstanceOf:
      return 8;
    case BinaryOperator.LessThanLessThan:
    case BinaryOperator.GreaterThanGreaterThan:
    case BinaryOperator.GreaterThanGreaterThanGreaterThan:
      return 9;
    case BinaryOperator.Add:
    case BinaryOperator.Subtract:
      return 10;
    case BinaryOperator.Multiply:
    case BinaryOperator.Divide:
    case BinaryOperator.Remainder:
      return 11;
    case BinaryOperator.Exponent:
      return 12;
    default:
      return -1;
  }
}

function prefixUnaryOperator(kind: TokenKind): PrefixUnaryOperator | undefined {
  switch (kind) {
    case TokenKind.Plus:
      return PrefixUnaryOperator.Plus;
    case TokenKind.Minus:
      return PrefixUnaryOperator.Minus;
    case TokenKind.Tilde:
      return PrefixUnaryOperator.Tilde;
    case TokenKind.Exclamation:
      return PrefixUnaryOperator.Exclamation;
    case TokenKind.TypeOfKeyword:
      return PrefixUnaryOperator.TypeOf;
    case TokenKind.VoidKeyword:
      return PrefixUnaryOperator.Void;
    case TokenKind.DeleteKeyword:
      return PrefixUnaryOperator.Delete;
    case TokenKind.PlusPlus:
      return PrefixUnaryOperator.PlusPlus;
    case TokenKind.MinusMinus:
      return PrefixUnaryOperator.MinusMinus;
    case TokenKind.AwaitKeyword:
      return PrefixUnaryOperator.Await;
    default:
      return undefined;
  }
}

/** Thrown internally to unwind a speculative parse; never escapes the parser. */
class SpeculationError extends Error {
  constructor() {
    super("speculation failed");
    this.name = "SpeculationError";
  }
}
