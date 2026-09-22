/**
 * Statement parsing: blocks, control flow, loops, try/catch and the modifier
 * handling shared by declarations.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { isIdentifierNameToken, isKeywordKind, isOfKeyword, TokenKind } from "../lexer/token.js";
import {
  ModifierKind,
  SyntaxKind,
  type Block,
  type CatchClause,
  type CaseClause,
  type DefaultClause,
  type Expression,
  type ForInStatement,
  type ForOfStatement,
  type ForStatement,
  type IfStatement,
  type Identifier,
  type Modifier,
  type Statement,
  type SwitchStatement,
  type TryStatement,
  type TypeNode,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../ast/nodes.js";
import type { Parser } from "./parser.js";

export interface StatementMethods {
  tryParseModifiers(this: Parser): Modifier[];
  hasModifier(this: Parser, modifiers: readonly Modifier[], kind: ModifierKind): boolean;
  parseStatement(this: Parser): Statement | undefined;
  parseStatementWithModifiers(this: Parser, modifiers: Modifier[]): Statement;
  parseBlock(this: Parser): Block;
  parseVariableStatement(this: Parser, modifiers: Modifier[]): VariableStatement;
  parseVariableDeclarationList(this: Parser): VariableDeclarationList;
  parseVariableDeclaration(this: Parser): VariableDeclaration;
  parseIfStatement(this: Parser): IfStatement;
  parseWhileStatement(this: Parser): Statement;
  parseDoStatement(this: Parser): Statement;
  parseForStatement(this: Parser): ForStatement | ForOfStatement | ForInStatement;
  parseReturnStatement(this: Parser): Statement;
  parseLabeledStatement(this: Parser): Statement;
  parseBreakStatement(this: Parser): Statement;
  parseContinueStatement(this: Parser): Statement;
  parseThrowStatement(this: Parser): Statement;
  parseTryStatement(this: Parser): TryStatement;
  parseSwitchStatement(this: Parser): SwitchStatement;
  parseExpressionStatement(this: Parser): Statement;
}

export const statementMethods: StatementMethods = {
  // -- modifiers ------------------------------------------------------------

  tryParseModifiers(this: Parser): Modifier[] {
    const modifiers: Modifier[] = [];
    for (;;) {
      const kind = this.token.kind;
      const nextKind = this.lookAhead(1).kind;
      const nextStartsName =
        isIdentifierNameToken(nextKind) ||
        isKeywordKind(nextKind) ||
        nextKind === TokenKind.OpenBracket ||
        nextKind === TokenKind.StringLiteral ||
        nextKind === TokenKind.NumericLiteral ||
        nextKind === TokenKind.Asterisk ||
        nextKind === TokenKind.OpenBrace;
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
        case TokenKind.ConstKeyword:
          if (this.atAhead(1, TokenKind.EnumKeyword)) modifierKind = ModifierKind.Const;
          break;
        case TokenKind.AsyncKeyword:
          if (!this.lookAhead(1).precededByLineBreak) modifierKind = ModifierKind.Async;
          break;
        default:
          break;
      }
      if (!modifierKind) break;
      // Contextual modifiers (`declare`, `readonly`, `static`, …) are only
      // modifiers when a member/declaration name follows; otherwise they are
      // names themselves (`private declare(...)`, `readonly()`).
      if (modifierKind !== ModifierKind.Export && modifierKind !== ModifierKind.Default && !nextStartsName) break;
      const token = this.nextToken();
      modifiers.push({ kind: SyntaxKind.Unknown, modifierKind, start: token.start, end: token.end });
    }
    return modifiers;
  },

  hasModifier(this: Parser, modifiers: readonly Modifier[], kind: ModifierKind): boolean {
    return modifiers.some((m) => m.modifierKind === kind);
  },

  // -- statements -----------------------------------------------------------

  parseStatement(this: Parser): Statement | undefined {
    const token = this.token;
    if (this.isIdentifierLike(token) && this.atAhead(1, TokenKind.Colon)) {
      return this.parseLabeledStatement();
    }
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
        if (this.at(TokenKind.ConstKeyword) && this.atAhead(1, TokenKind.EnumKeyword)) {
          const constToken = this.nextToken();
          return this.parseEnumDeclaration([{ kind: SyntaxKind.Unknown, modifierKind: ModifierKind.Const, start: constToken.start, end: constToken.end }]);
        }
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
  },

  parseStatementWithModifiers(this: Parser, modifiers: Modifier[]): Statement {
    switch (this.token.kind) {
      case TokenKind.VarKeyword:
      case TokenKind.LetKeyword:
      case TokenKind.ConstKeyword:
        if (this.at(TokenKind.ConstKeyword) && this.atAhead(1, TokenKind.EnumKeyword)) {
          const constToken = this.nextToken();
          return this.parseEnumDeclaration([...modifiers, { kind: SyntaxKind.Unknown, modifierKind: ModifierKind.Const, start: constToken.start, end: constToken.end }]);
        }
        return this.parseVariableStatement(modifiers);
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
  },

  parseBlock(this: Parser): Block {
    const open = this.parseExpected(TokenKind.OpenBrace);
    const statements: Statement[] = [];
    while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
      const before = this.consumed;
      const s = this.parseStatement();
      if (s) statements.push(s);
      if (this.consumed === before) {
        this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${this.token.text}'`, this.token);
        this.nextToken();
      }
    }
    const close = this.parseExpected(TokenKind.CloseBrace);
    return { kind: SyntaxKind.Block, statements, start: open.start, end: close.end };
  },

  parseVariableStatement(this: Parser, modifiers: Modifier[]): VariableStatement {
    const declList = this.parseVariableDeclarationList();
    this.parseSemicolon();
    return { kind: SyntaxKind.VariableStatement, declarationList: declList, modifiers, start: declList.start, end: declList.end };
  },

  parseVariableDeclarationList(this: Parser): VariableDeclarationList {
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
  },

  parseVariableDeclaration(this: Parser): VariableDeclaration {
    const name = this.parseBindingName();
    let exclamation = false;
    if (name.kind === SyntaxKind.Identifier && this.at(TokenKind.Exclamation)) {
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
  },

  parseIfStatement(this: Parser): IfStatement {
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
  },

  parseWhileStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.WhileKeyword).start;
    this.parseExpected(TokenKind.OpenParen);
    const condition = this.parseExpression();
    this.parseExpected(TokenKind.CloseParen);
    const statement = this.parseStatement()!;
    return { kind: SyntaxKind.WhileStatement, condition, statement, start, end: statement.end } as Statement;
  },

  parseDoStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.DoKeyword).start;
    const statement = this.parseStatement()!;
    this.parseExpected(TokenKind.WhileKeyword);
    this.parseExpected(TokenKind.OpenParen);
    const condition = this.parseExpression();
    const close = this.parseExpected(TokenKind.CloseParen);
    this.parseSemicolon();
    return { kind: SyntaxKind.DoStatement, condition, statement, start, end: close.end } as Statement;
  },

  parseForStatement(this: Parser): ForStatement | ForOfStatement | ForInStatement {
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
  },

  parseReturnStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.ReturnKeyword).start;
    let expression: Expression | undefined;
    if (!this.at(TokenKind.Semicolon) && !this.canInsertSemicolon()) expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ReturnStatement, expression, start, end: expression?.end ?? start + 6 } as Statement;
  },

  parseLabeledStatement(this: Parser): Statement {
    const label = this.parseIdentifier();
    this.parseExpected(TokenKind.Colon);
    const statement = this.parseStatement();
    const node = statement ?? ({ kind: SyntaxKind.EmptyStatement, start: label.end, end: label.end } as Statement);
    return { kind: SyntaxKind.LabeledStatement, label, statement: node, start: label.start, end: node.end } as Statement;
  },

  parseBreakStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.BreakKeyword).start;
    let label: Identifier | undefined;
    if (!this.canInsertSemicolon() && !this.at(TokenKind.Semicolon)) label = this.parseIdentifier();
    this.parseSemicolon();
    return { kind: SyntaxKind.BreakStatement, label, start, end: label?.end ?? start + 5 } as Statement;
  },

  parseContinueStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.ContinueKeyword).start;
    let label: Identifier | undefined;
    if (!this.canInsertSemicolon() && !this.at(TokenKind.Semicolon)) label = this.parseIdentifier();
    this.parseSemicolon();
    return { kind: SyntaxKind.ContinueStatement, label, start, end: label?.end ?? start + 8 } as Statement;
  },

  parseThrowStatement(this: Parser): Statement {
    const start = this.parseExpected(TokenKind.ThrowKeyword).start;
    const expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ThrowStatement, expression, start, end: expression.end } as Statement;
  },

  parseTryStatement(this: Parser): TryStatement {
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
  },

  parseSwitchStatement(this: Parser): SwitchStatement {
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
  },

  parseExpressionStatement(this: Parser): Statement {
    const expression = this.parseExpression();
    this.parseSemicolon();
    return { kind: SyntaxKind.ExpressionStatement, expression, start: expression.start, end: expression.end };
  },
};
