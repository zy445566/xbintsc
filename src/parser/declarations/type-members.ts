/**
 * Type-level declaration parsing: interfaces, type aliases, enums, namespaces
 * and interface/type members.
 */

import { TokenKind } from "../../lexer/token.js";
import {
  SyntaxKind,
  type Block,
  type EnumDeclaration,
  type EnumMember,
  type Expression,
  type Identifier,
  type InterfaceDeclaration,
  type MethodSignature,
  type Modifier,
  type ModuleDeclaration,
  type Parameter,
  type PropertySignature,
  type StringLiteral,
  type TypeAliasDeclaration,
  type TypeElement,
  type TypeNode,
} from "../../ast/nodes.js";
import { SpeculationError } from "../speculation.js";
import type { Parser } from "../parser.js";

export interface DeclarationTypeMethods {
  parseInterfaceDeclaration(this: Parser, modifiers: Modifier[]): InterfaceDeclaration;
  parseTypeAliasDeclaration(this: Parser, modifiers: Modifier[]): TypeAliasDeclaration;
  parseEnumDeclaration(this: Parser, modifiers: Modifier[]): EnumDeclaration;
  parseModuleDeclaration(this: Parser, modifiers: Modifier[]): ModuleDeclaration;
  parseTypeMembers(this: Parser): TypeElement[];
  parseTypeMember(this: Parser): TypeElement | undefined;
  parseSemicolonOrComma(this: Parser): void;
}

export const declarationTypeMethods: DeclarationTypeMethods = {
  parseInterfaceDeclaration(this: Parser, modifiers: Modifier[]): InterfaceDeclaration {
    const start = this.parseExpected(TokenKind.InterfaceKeyword).start;
    const name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseTypeMembers();
    return { kind: SyntaxKind.InterfaceDeclaration, name, modifiers, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? name.end };
  },

  parseTypeAliasDeclaration(this: Parser, modifiers: Modifier[]): TypeAliasDeclaration {
    const start = this.parseExpected(TokenKind.TypeKeyword).start;
    const name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    this.parseExpected(TokenKind.Equals);
    const type = this.parseType();
    this.parseSemicolon();
    return { kind: SyntaxKind.TypeAliasDeclaration, name, modifiers, typeParameters, type, start, end: type.end };
  },

  parseEnumDeclaration(this: Parser, modifiers: Modifier[]): EnumDeclaration {
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
  },

  parseModuleDeclaration(this: Parser, modifiers: Modifier[]): ModuleDeclaration {
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
  },

  parseTypeMembers(this: Parser): TypeElement[] {
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
  },

  parseTypeMember(this: Parser): TypeElement | undefined {
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
    if (this.at(TokenKind.OpenBracket)) {
      const indexSignature = this.tryParse<TypeElement>(() => {
        const open = this.nextToken();
        const parameters: Parameter[] = [];
        const paramName = this.parseIdentifier();
        let paramType: TypeNode | undefined;
        if (this.at(TokenKind.Colon)) {
          this.nextToken();
          paramType = this.parseType();
        } else {
          throw new SpeculationError();
        }
        this.parseExpected(TokenKind.CloseBracket);
        if (!this.at(TokenKind.Colon)) throw new SpeculationError();
        this.nextToken();
        const type = this.parseType();
        this.parseSemicolonOrComma();
        parameters.push({ kind: SyntaxKind.Parameter, name: paramName, modifiers: [], dotDotDotToken: false, questionToken: false, type: paramType, start: paramName.start, end: paramType?.end ?? paramName.end });
        return { kind: SyntaxKind.IndexSignature, parameters, type, start: open.start, end: type.end };
      });
      if (indexSignature) return indexSignature;
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
  },

  parseSemicolonOrComma(this: Parser): void {
    if (this.at(TokenKind.Semicolon) || this.at(TokenKind.Comma)) this.nextToken();
  },
};
