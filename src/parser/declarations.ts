/**
 * Declaration parsing: functions, classes, interfaces, type aliases, enums,
 * namespaces and type members.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { TokenKind } from "../lexer/token.js";
import {
  ModifierKind,
  NodeFlags,
  SyntaxKind,
  type Block,
  type ClassDeclaration,
  type ClassElement,
  type ConstructorDeclaration,
  type EnumDeclaration,
  type EnumMember,
  type Expression,
  type ExpressionWithTypeArguments,
  type FunctionDeclaration,
  type HeritageClause,
  type Identifier,
  type IndexSignature,
  type InterfaceDeclaration,
  type MethodDeclaration,
  type MethodSignature,
  type Modifier,
  type ModuleDeclaration,
  type Parameter,
  type PropertyDeclaration,
  type PropertyName,
  type PropertySignature,
  type StringLiteral,
  type TypeAliasDeclaration,
  type TypeElement,
  type TypeNode,
  type TypeParameterDeclaration,
} from "../ast/nodes.js";
import { expressionToPropertyName } from "./helpers.js";
import type { Parser } from "./parser.js";

export interface DeclarationMethods {
  parseFunctionDeclaration(this: Parser, modifiers: Modifier[]): FunctionDeclaration;
  parseTypeParameters(this: Parser): TypeParameterDeclaration[];
  parseGreaterThan(this: Parser): void;
  parseParameters(this: Parser): Parameter[];
  parseReturnType(this: Parser): TypeNode;
  parseClassDeclaration(this: Parser, modifiers: Modifier[]): ClassDeclaration;
  parseHeritageClauses(this: Parser): HeritageClause[];
  parseClassMembers(this: Parser): ClassElement[];
  parseClassElement(this: Parser): ClassElement | undefined;
  parsePropertyName(this: Parser): PropertyName;
  parseInterfaceDeclaration(this: Parser, modifiers: Modifier[]): InterfaceDeclaration;
  parseTypeAliasDeclaration(this: Parser, modifiers: Modifier[]): TypeAliasDeclaration;
  parseEnumDeclaration(this: Parser, modifiers: Modifier[]): EnumDeclaration;
  parseModuleDeclaration(this: Parser, modifiers: Modifier[]): ModuleDeclaration;
  parseTypeMembers(this: Parser): TypeElement[];
  parseTypeMember(this: Parser): TypeElement | undefined;
  parseSemicolonOrComma(this: Parser): void;
}

export const declarationMethods: DeclarationMethods = {
  // -- functions ------------------------------------------------------------

  parseFunctionDeclaration(this: Parser, modifiers: Modifier[]): FunctionDeclaration {
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
  },

  parseTypeParameters(this: Parser): TypeParameterDeclaration[] {
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
  },

  /** Consume a `>` that may have been lexed as `>>` / `>>>`, splitting it. */
  parseGreaterThan(this: Parser): void {
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
  },

  parseParameters(this: Parser): Parameter[] {
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
  },

  parseReturnType(this: Parser): TypeNode {
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
  },

  // -- classes --------------------------------------------------------------

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
      this.error(DiagnosticCode.InvalidTypeSyntax, "Computed property names are not supported by xbintsc");
      return { kind: SyntaxKind.Identifier, text: "<computed>", start: expr.start, end: expr.end };
    }
    return this.parseIdentifierName();
  },

  // -- interfaces / aliases / enums / namespaces ----------------------------

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

  // -- type members ---------------------------------------------------------

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
  },

  parseSemicolonOrComma(this: Parser): void {
    if (this.at(TokenKind.Semicolon) || this.at(TokenKind.Comma)) this.nextToken();
  },
};
