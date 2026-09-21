/**
 * Function declaration parsing: function signatures, type parameters,
 * parameter lists, binding names and return types.
 */

import { TokenKind } from "../../lexer/token.js";
import {
  NodeFlags,
  ModifierKind,
  SyntaxKind,
  type Block,
  type Expression,
  type FunctionDeclaration,
  type Identifier,
  type Modifier,
  type Parameter,
  type PropertyName,
  type TypeNode,
  type TypeParameterDeclaration,
} from "../../ast/nodes.js";
import type { BindingElement, BindingName } from "../../ast/declarations.js";
import { expressionToPropertyName } from "../helpers.js";
import type { Parser } from "../parser.js";

export interface DeclarationFunctionMethods {
  parseFunctionDeclaration(this: Parser, modifiers: Modifier[]): FunctionDeclaration;
  parseTypeParameters(this: Parser): TypeParameterDeclaration[];
  parseGreaterThan(this: Parser): void;
  parseParameters(this: Parser): Parameter[];
  parseBindingName(this: Parser): BindingName;
  parseBindingElement(this: Parser, allowPropertyName: boolean): BindingElement;
  parseReturnType(this: Parser): TypeNode;
}

export const declarationFunctionMethods: DeclarationFunctionMethods = {
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
      let name: BindingName;
      let isThisParameter = false;
      if (this.at(TokenKind.ThisKeyword)) {
        const thisToken = this.nextToken();
        name = { kind: SyntaxKind.Identifier, text: "this", start: thisToken.start, end: thisToken.end };
        isThisParameter = true;
      } else {
        name = this.parseBindingName();
      }
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
      // A `this` parameter is a type-level annotation only and is never a
      // runtime argument, so it must not occupy an `argv` slot.
      if (!isThisParameter) {
        params.push({ kind: SyntaxKind.Parameter, name, modifiers, dotDotDotToken, questionToken, type, initializer, start, end: initializer?.end ?? type?.end ?? name.end });
      }
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseExpected(TokenKind.CloseParen);
    return params;
  },

  parseBindingName(this: Parser): BindingName {
    if (this.at(TokenKind.OpenBracket)) {
      const open = this.nextToken();
      const elements: (BindingElement | undefined)[] = [];
      while (!this.at(TokenKind.CloseBracket) && !this.at(TokenKind.EndOfFile)) {
        if (this.at(TokenKind.Comma)) {
          elements.push(undefined);
          this.nextToken();
          continue;
        }
        elements.push(this.parseBindingElement(false));
        if (this.at(TokenKind.Comma)) this.nextToken();
        else break;
      }
      const close = this.parseExpected(TokenKind.CloseBracket);
      return { kind: SyntaxKind.ArrayBindingPattern, elements, start: open.start, end: close.end };
    }
    if (this.at(TokenKind.OpenBrace)) {
      const open = this.nextToken();
      const elements: BindingElement[] = [];
      while (!this.at(TokenKind.CloseBrace) && !this.at(TokenKind.EndOfFile)) {
        elements.push(this.parseBindingElement(true));
        if (this.at(TokenKind.Comma)) this.nextToken();
        else break;
      }
      const close = this.parseExpected(TokenKind.CloseBrace);
      return { kind: SyntaxKind.ObjectBindingPattern, elements, start: open.start, end: close.end };
    }
    return this.parseIdentifier();
  },

  parseBindingElement(this: Parser, allowPropertyName: boolean): BindingElement {
    const start = this.token.start;
    let dotDotDotToken = false;
    if (this.at(TokenKind.DotDotDot)) {
      this.nextToken();
      dotDotDotToken = true;
    }
    let propertyName: PropertyName | undefined;
    let name: BindingName;
    if (allowPropertyName && this.at(TokenKind.OpenBracket)) {
      // Computed property key: `{ [expr]: target }`.
      this.nextToken();
      const expr = this.parseAssignmentExpression();
      this.parseExpected(TokenKind.CloseBracket);
      propertyName = expressionToPropertyName(expr) ?? { kind: SyntaxKind.Identifier, text: "<computed>", start: expr.start, end: expr.end };
      this.parseExpected(TokenKind.Colon);
      name = this.parseBindingName();
    } else if (allowPropertyName && !this.at(TokenKind.OpenBrace) && !this.at(TokenKind.OpenBracket)) {
      const first = this.parseIdentifierName();
      if (this.at(TokenKind.Colon)) {
        this.nextToken();
        propertyName = first;
        name = this.parseBindingName();
      } else {
        name = first;
      }
    } else {
      // Array element, or a nested pattern used as an object value.
      name = this.parseBindingName();
    }
    let initializer: Expression | undefined;
    if (this.at(TokenKind.Equals)) {
      this.nextToken();
      initializer = this.parseAssignmentExpression();
    }
    const end = initializer?.end ?? (name.kind === SyntaxKind.Identifier ? name.end : name.end);
    return { kind: SyntaxKind.BindingElement, name, propertyName, dotDotDotToken, initializer, start, end };
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
};
