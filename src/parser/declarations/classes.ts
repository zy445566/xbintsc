/**
 * Class declaration parsing: class bodies, heritage clauses, members and
 * property names.
 */

import { TokenKind } from "../../lexer/token.js";
import {
  NodeFlags,
  ModifierKind,
  SyntaxKind,
  type Block,
  type ClassDeclaration,
  type ClassElement,
  type ConstructorDeclaration,
  type Expression,
  type ExpressionWithTypeArguments,
  type HeritageClause,
  type Identifier,
  type MethodDeclaration,
  type Modifier,
  type PropertyDeclaration,
  type PropertyName,
  type TypeNode,
} from "../../ast/nodes.js";
import type { Parser } from "../parser.js";

export interface DeclarationClassMethods {
  parseClassDeclaration(this: Parser, modifiers: Modifier[]): ClassDeclaration;
  parseHeritageClauses(this: Parser): HeritageClause[];
  parseClassMembers(this: Parser): ClassElement[];
  parseClassElement(this: Parser): ClassElement | undefined;
  parsePropertyName(this: Parser): PropertyName;
}

export const declarationClassMethods: DeclarationClassMethods = {
  parseClassDeclaration(this: Parser, modifiers: Modifier[]): ClassDeclaration {
    const start = this.parseExpected(TokenKind.ClassKeyword).start;
    let name: Identifier | undefined;
    if (!this.at(TokenKind.OpenBrace) && !this.at(TokenKind.ExtendsKeyword) && !this.at(TokenKind.ImplementsKeyword)) {
      name = this.parseIdentifier();
    }
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseClassMembers();
    return { kind: SyntaxKind.ClassDeclaration, name, modifiers, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? start };
  },

  parseHeritageClauses(this: Parser): HeritageClause[] {
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
  },

  parseClassMembers(this: Parser): ClassElement[] {
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
  },

  parseClassElement(this: Parser): ClassElement | undefined {
    const start = this.token.start;
    const modifiers = this.tryParseModifiers();
    if (this.at(TokenKind.Semicolon)) {
      this.nextToken();
      return undefined;
    }
    if (
      (this.at(TokenKind.GetKeyword) || this.at(TokenKind.SetKeyword)) &&
      !this.atAhead(1, TokenKind.OpenParen) &&
      !this.atAhead(1, TokenKind.Colon) &&
      !this.atAhead(1, TokenKind.Equals) &&
      !this.atAhead(1, TokenKind.Question) &&
      !this.atAhead(1, TokenKind.LessThan) &&
      !this.atAhead(1, TokenKind.Semicolon)
    ) {
      const accessorToken = this.nextToken();
      const name = this.parsePropertyName();
      const parameters = this.parseParameters();
      let returnType: TypeNode | undefined;
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        returnType = this.parseReturnType();
      }
      let body: Block | undefined;
      if (this.at(TokenKind.OpenBrace)) body = this.parseBlock();
      else this.parseSemicolon();
      const accessor: MethodDeclaration = {
        kind: SyntaxKind.MethodDeclaration,
        name,
        modifiers,
        typeParameters: [],
        parameters,
        returnType,
        body,
        flags: NodeFlags.None,
        optional: false,
        accessor: accessorToken.kind === TokenKind.GetKeyword ? "get" : "set",
        start,
        end: body?.end ?? returnType?.end ?? name.end,
      };
      return accessor;
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
  },

  parsePropertyName(this: Parser): PropertyName {
    const token = this.token;
    if (token.kind === TokenKind.PrivateIdentifier) {
      this.nextToken();
      return { kind: SyntaxKind.PrivateIdentifier, text: token.text, start: token.start, end: token.end };
    }
    if (token.kind === TokenKind.StringLiteral) {
      this.nextToken();
      return { kind: SyntaxKind.StringLiteral, text: token.text, raw: token.text, value: String(token.value ?? ""), start: token.start, end: token.end };
    }
    if (token.kind === TokenKind.NumericLiteral) {
      this.nextToken();
      return { kind: SyntaxKind.NumericLiteral, text: token.text, value: Number(token.value ?? 0), start: token.start, end: token.end };
    }
    if (token.kind === TokenKind.OpenBracket) {
      const start = this.nextToken().start;
      const expr = this.parseAssignmentExpression();
      const close = this.parseExpected(TokenKind.CloseBracket);
      return { kind: SyntaxKind.ComputedPropertyName, expression: expr, start, end: close.end };
    }
    return this.parseIdentifierName();
  },
};
