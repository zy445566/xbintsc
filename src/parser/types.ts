/**
 * Type parsing: unions, intersections, operators, references, function/tuple
 * types, type literals and mapped types.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { TokenKind } from "../lexer/token.js";
import { TypeOperator } from "../ast/operators.js";
import {
  SyntaxKind,
  type BooleanLiteral,
  type FunctionTypeNode,
  type Identifier,
  type MappedTypeNode,
  type NumericLiteral,
  type QualifiedName,
  type TupleTypeNode,
  type TypeLiteralNode,
  type TypeNode,
  type TypeParameterDeclaration,
} from "../ast/nodes.js";
import type { Parser } from "./parser.js";

export interface TypeMethods {
  parseTypeArguments(this: Parser): TypeNode[];
  parseType(this: Parser): TypeNode;
  parseUnionType(this: Parser): TypeNode;
  parseIntersectionType(this: Parser): TypeNode;
  parseTypeOperatorOrHigher(this: Parser): TypeNode;
  parsePostfixType(this: Parser): TypeNode;
  parsePrimaryType(this: Parser): TypeNode;
  previousTypeEnd(this: Parser, typeArguments: readonly TypeNode[], fallback: number): number;
  parseEntityName(this: Parser): Identifier | QualifiedName;
  parseFunctionType(this: Parser, explicitStart?: number): FunctionTypeNode;
  parseTupleType(this: Parser): TupleTypeNode;
  parseTypeLiteral(this: Parser): TypeLiteralNode | MappedTypeNode;
}

export const typeMethods: TypeMethods = {
  parseTypeArguments(this: Parser): TypeNode[] {
    this.parseExpected(TokenKind.LessThan);
    const types: TypeNode[] = [];
    while (!this.at(TokenKind.GreaterThan) && !this.at(TokenKind.EndOfFile)) {
      types.push(this.parseType());
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    this.parseGreaterThan();
    return types;
  },

  // -- types ----------------------------------------------------------------

  parseType(this: Parser): TypeNode {
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
  },

  parseUnionType(this: Parser): TypeNode {
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
  },

  parseIntersectionType(this: Parser): TypeNode {
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
  },

  parseTypeOperatorOrHigher(this: Parser): TypeNode {
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
  },

  parsePostfixType(this: Parser): TypeNode {
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
  },

  parsePrimaryType(this: Parser): TypeNode {
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
        if (this.at(TokenKind.ImportKeyword)) {
          // `typeof import("mod")` / `typeof import("mod").T`: erase to `any`.
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
          return { kind: SyntaxKind.AnyKeywordType, start, end: this.token.start };
        }
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
      case TokenKind.ConstKeyword: {
        // `as const` const assertion: erased like any other type.
        this.nextToken();
        return { kind: SyntaxKind.AnyKeywordType, start, end: token.end };
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
  },

  previousTypeEnd(this: Parser, typeArguments: readonly TypeNode[], fallback: number): number {
    return typeArguments[typeArguments.length - 1]?.end ?? fallback;
  },

  parseEntityName(this: Parser): Identifier | QualifiedName {
    let left: Identifier | QualifiedName = this.parseIdentifierName();
    while (this.at(TokenKind.Dot)) {
      this.nextToken();
      const right = this.parseIdentifierName();
      left = { kind: SyntaxKind.QualifiedName, left, right, start: left.start, end: right.end };
    }
    return left;
  },

  parseFunctionType(this: Parser, explicitStart?: number): FunctionTypeNode {
    const start = explicitStart ?? this.token.start;
    const typeParameters = this.at(TokenKind.LessThan) ? this.parseTypeParameters() : [];
    const parameters = this.parseParameters();
    this.parseExpected(TokenKind.EqualsGreaterThan);
    const returnType = this.parseReturnType();
    return { kind: SyntaxKind.FunctionType, typeParameters, parameters, returnType, start, end: returnType.end };
  },

  parseTupleType(this: Parser): TupleTypeNode {
    const open = this.parseExpected(TokenKind.OpenBracket);
    const elements: TypeNode[] = [];
    while (!this.at(TokenKind.CloseBracket) && !this.at(TokenKind.EndOfFile)) {
      const start = this.token.start;
      let element: TypeNode;
      if (this.at(TokenKind.DotDotDot)) {
        this.nextToken();
        // A rest member may carry a label: `...rest: T[]`.
        if (
          this.isIdentifierLike(this.token) &&
          (this.atAhead(1, TokenKind.Colon) || (this.atAhead(1, TokenKind.Question) && this.atAhead(2, TokenKind.Colon)))
        ) {
          this.parseIdentifierName();
          if (this.at(TokenKind.Question)) this.nextToken();
          this.parseExpected(TokenKind.Colon);
        }
        const type = this.parseType();
        element = { kind: SyntaxKind.RestType, type, start, end: type.end };
      } else if (
        this.isIdentifierLike(this.token) &&
        (this.atAhead(1, TokenKind.Colon) || (this.atAhead(1, TokenKind.Question) && this.atAhead(2, TokenKind.Colon)))
      ) {
        // Named tuple member: `code: number`, `arg?: unknown`. The label is a
        // type-level annotation with no runtime effect, so it is dropped.
        this.parseIdentifierName();
        let question = false;
        if (this.at(TokenKind.Question)) {
          this.nextToken();
          question = true;
        }
        this.parseExpected(TokenKind.Colon);
        const type = this.parseType();
        element = question ? { kind: SyntaxKind.OptionalType, type, start, end: type.end } : type;
      } else {
        element = this.parseType();
        if (this.at(TokenKind.Question)) {
          const end = this.nextToken().end;
          element = { kind: SyntaxKind.OptionalType, type: element, start: element.start, end };
        }
      }
      elements.push(element);
      if (this.at(TokenKind.Comma)) this.nextToken();
      else break;
    }
    const close = this.parseExpected(TokenKind.CloseBracket);
    return { kind: SyntaxKind.TupleType, elements, start: open.start, end: close.end };
  },

  parseTypeLiteral(this: Parser): TypeLiteralNode | MappedTypeNode {
    const start = this.token.start;
    // Detect a mapped type, skipping an optional `readonly` / `+readonly` /
    // `-readonly` modifier before the `[`.
    let ahead = 1;
    if (this.lookAhead(ahead).kind === TokenKind.ReadonlyKeyword) {
      ahead++;
    } else if (
      (this.lookAhead(ahead).kind === TokenKind.Plus || this.lookAhead(ahead).kind === TokenKind.Minus) &&
      this.lookAhead(ahead + 1).kind === TokenKind.ReadonlyKeyword
    ) {
      ahead += 2;
    }
    if (
      this.lookAhead(ahead).kind === TokenKind.OpenBracket &&
      this.lookAhead(ahead + 1).kind === TokenKind.Identifier &&
      this.lookAhead(ahead + 2).kind === TokenKind.InKeyword
    ) {
      const open = this.parseExpected(TokenKind.OpenBrace);
      let readonlyToken: boolean | "+" | "-" = false;
      if (this.at(TokenKind.ReadonlyKeyword)) {
        this.nextToken();
        readonlyToken = true;
      } else if (this.at(TokenKind.Plus) && this.atAhead(1, TokenKind.ReadonlyKeyword)) {
        this.nextToken();
        this.nextToken();
        readonlyToken = "+";
      } else if (this.at(TokenKind.Minus) && this.atAhead(1, TokenKind.ReadonlyKeyword)) {
        this.nextToken();
        this.nextToken();
        readonlyToken = "-";
      }
      this.parseExpected(TokenKind.OpenBracket);
      const name = this.parseIdentifier();
      this.parseExpected(TokenKind.InKeyword);
      const constraint = this.parseType();
      let nameType: TypeNode | undefined;
      if (this.at(TokenKind.AsKeyword)) {
        this.nextToken();
        nameType = this.parseType();
      }
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
      return { kind: SyntaxKind.MappedType, readonlyToken, typeParameter, nameType, questionToken, type, start: open.start, end: close.end };
    }
    const members = this.parseTypeMembers();
    return { kind: SyntaxKind.TypeLiteral, members, start, end: this.token.start };
  },
};
