/**
 * Import and export declaration parsing.
 */

import { TokenKind } from "../lexer/token.js";
import {
  ModifierKind,
  SyntaxKind,
  type ExportAssignment,
  type ExportDeclaration,
  type ExportSpecifier,
  type Identifier,
  type ImportClause,
  type ImportDeclaration,
  type ImportSpecifier,
  type Modifier,
  type NamedExports,
  type NamedImports,
  type NamespaceImport,
  type Statement,
  type StringLiteral,
} from "../ast/nodes.js";
import { propertyNameText } from "./helpers.js";
import type { Parser } from "./parser.js";

export interface ModuleMethods {
  parseImportDeclaration(this: Parser): ImportDeclaration;
  parseNamedImports(this: Parser): NamedImports;
  parseExportDeclaration(this: Parser): Statement;
  parseNamedExports(this: Parser): NamedExports;
}

export const moduleMethods: ModuleMethods = {
  parseImportDeclaration(this: Parser): ImportDeclaration {
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
  },

  parseNamedImports(this: Parser): NamedImports {
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
  },

  parseExportDeclaration(this: Parser): Statement {
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
    let isTypeOnly = false;
    if (this.at(TokenKind.TypeKeyword) && (this.atAhead(1, TokenKind.OpenBrace) || this.atAhead(1, TokenKind.Asterisk))) {
      isTypeOnly = true;
      this.nextToken();
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
        isTypeOnly,
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
      const decl: ExportDeclaration = { kind: SyntaxKind.ExportDeclaration, modifiers: [], exportClause: namedExports, moduleSpecifier, isTypeOnly, start, end: moduleSpecifier?.end ?? namedExports.end };
      return decl;
    }
    const modifiers = this.tryParseModifiers();
    modifiers.push({ kind: SyntaxKind.Unknown, modifierKind: ModifierKind.Export, start, end: this.token.start });
    return this.parseStatementWithModifiers(modifiers);
  },

  parseNamedExports(this: Parser): NamedExports {
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
  },
};
