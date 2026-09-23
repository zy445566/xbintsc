/**
 * Expression parsing: assignment/conditional/binary/unary operators, calls,
 * member access, literals, arrow functions, templates and regexes.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { TokenKind } from "../lexer/token.js";
import {
  BinaryOperator,
  PostfixUnaryOperator,
  PrefixUnaryOperator,
} from "../ast/operators.js";
import {
  NodeFlags,
  SyntaxKind,
  type ArrowFunction,
  type BinaryExpression,
  type BooleanLiteral,
  type CallExpression,
  type ConditionalExpression,
  type Expression,
  type Identifier,
  type NewExpression,
  type NumericLiteral,
  type Parameter,
  type ParenthesizedExpression,
  type TypeNode,
} from "../ast/nodes.js";
import {
  AS_PRECEDENCE,
  assignmentOperator,
  binaryOperator,
  binaryPrecedence,
  prefixUnaryOperator,
} from "./operators.js";
import { isAssignmentTarget } from "./helpers.js";
import { SpeculationError } from "./speculation.js";
import type { Parser } from "./parser.js";

export interface ExpressionMethods {
  parseExpression(this: Parser, noIn?: boolean): Expression;
  parseAssignmentExpression(this: Parser, noIn?: boolean): Expression;
  tryParseArrowFunction(this: Parser): ArrowFunction | undefined;
  countTypeParameterTokens(this: Parser, cursor: number): number;
  findMatchingParen(this: Parser, cursor: number): number;
  skipTypeTokens(this: Parser, startIndex: number): number;
  parseConditionalExpression(this: Parser, noIn: boolean): Expression;
  parseBinaryExpression(this: Parser, minPrecedence: number, noIn: boolean): Expression;
  parseUnaryExpression(this: Parser): Expression;
  parsePostfixExpression(this: Parser): Expression;
  parseLeftHandSideExpression(this: Parser): Expression;
  parseNewExpression(this: Parser): NewExpression;
  parseMemberOnlyExpression(this: Parser): Expression;
  parsePrimaryExpression(this: Parser): Expression;
  parseCallAndMemberTail(this: Parser, base: Expression): Expression;
  previousEnd(this: Parser, args: readonly Expression[], fallback: number): number;
  parsePropertyAccess(this: Parser, expression: Expression): Expression;
  parseMemberName(this: Parser): Identifier;
  parseArguments(this: Parser): Expression[];
}

export const expressionMethods: ExpressionMethods = {
  parseExpression(this: Parser, noIn = false): Expression {
    return this.parseAssignmentExpression(noIn);
  },

  parseAssignmentExpression(this: Parser, noIn = false): Expression {
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
  },

  tryParseArrowFunction(this: Parser): ArrowFunction | undefined {
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
    const body: import("../ast/nodes.js").Block | Expression = this.at(TokenKind.OpenBrace) ? this.parseBlock() : this.parseAssignmentExpression();
    return { kind: SyntaxKind.ArrowFunction, typeParameters: typeParams, parameters, returnType, body, flags, start, end: body.end };
  },

  countTypeParameterTokens(this: Parser, cursor: number): number {
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
  },

  findMatchingParen(this: Parser, cursor: number): number {
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
  },

  skipTypeTokens(this: Parser, startIndex: number): number {
    let depth = 0;
    for (let i = startIndex; ; i++) {
      const t = this.lookAhead(i);
      if (t.kind === TokenKind.EndOfFile) return -1;
      if (t.kind === TokenKind.OpenParen || t.kind === TokenKind.OpenBracket || t.kind === TokenKind.OpenBrace) depth++;
      else if (t.kind === TokenKind.CloseParen || t.kind === TokenKind.CloseBracket || t.kind === TokenKind.CloseBrace) {
        if (depth === 0) return -1;
        depth--;
      } else if (t.kind === TokenKind.EqualsGreaterThan && depth === 0) return i;
      else if (depth === 0 && (t.kind === TokenKind.Semicolon || t.kind === TokenKind.Comma)) return -1;
    }
  },

  parseConditionalExpression(this: Parser, noIn: boolean): Expression {
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
  },

  parseBinaryExpression(this: Parser, minPrecedence: number, noIn: boolean): Expression {
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
  },

  parseUnaryExpression(this: Parser): Expression {
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
  },

  parsePostfixExpression(this: Parser): Expression {
    const start = this.token.start;
    const expression = this.parseLeftHandSideExpression();
    const token = this.token;
    if ((token.kind === TokenKind.PlusPlus || token.kind === TokenKind.MinusMinus) && !token.precededByLineBreak) {
      this.nextToken();
      const operator = token.kind === TokenKind.PlusPlus ? PostfixUnaryOperator.PlusPlus : PostfixUnaryOperator.MinusMinus;
      return { kind: SyntaxKind.PostfixUnaryExpression, operator, operand: expression, start, end: token.end };
    }
    return expression;
  },

  parseLeftHandSideExpression(this: Parser): Expression {
    const expression = this.at(TokenKind.NewKeyword) ? this.parseNewExpression() : this.parsePrimaryExpression();
    return this.parseCallAndMemberTail(expression);
  },

  parseNewExpression(this: Parser): NewExpression {
    const start = this.parseExpected(TokenKind.NewKeyword).start;
    const callee = this.parseMemberOnlyExpression();
    const typeArguments = this.at(TokenKind.LessThan) ? this.parseTypeArguments() : [];
    const args = this.at(TokenKind.OpenParen) ? this.parseArguments() : [];
    return { kind: SyntaxKind.NewExpression, expression: callee, typeArguments, arguments: args, start, end: args[args.length - 1]?.end ?? callee.end };
  },

  parseMemberOnlyExpression(this: Parser): Expression {
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
  },

  parsePrimaryExpression(this: Parser): Expression {
    const token = this.token;
    switch (token.kind) {
      case TokenKind.NumericLiteral: {
        this.nextToken();
        const node: NumericLiteral = { kind: SyntaxKind.NumericLiteral, text: token.text, value: Number(token.value ?? 0), start: token.start, end: token.end };
        return node;
      }
      case TokenKind.BigIntLiteral:
        this.nextToken();
        return { kind: SyntaxKind.BigIntLiteral, text: token.text, value: (token.value as bigint | undefined) ?? 0n, start: token.start, end: token.end };
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
      case TokenKind.SuperKeyword:
        this.nextToken();
        return { kind: SyntaxKind.Identifier, text: "super", start: token.start, end: token.end };
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
      case TokenKind.RegularExpressionLiteral:
        return this.parseRegularExpression();
      case TokenKind.ImportKeyword:
        if (this.atAhead(1, TokenKind.Dot)) {
          const importToken = this.nextToken();
          this.nextToken();
          const metaName = this.parseIdentifierName();
          return { kind: SyntaxKind.Identifier, text: "import.meta", start: importToken.start, end: metaName.end };
        }
        break;
      default:
        break;
    }
    if (this.isIdentifierLike(token)) return this.parseIdentifierName();
    this.error(DiagnosticCode.UnexpectedToken, `Unexpected token '${token.text || token.kind}'`);
    if (token.kind !== TokenKind.EndOfFile) this.nextToken();
    return { kind: SyntaxKind.Identifier, text: "<error>", start: token.start, end: token.end };
  },

  parseCallAndMemberTail(this: Parser, base: Expression): Expression {
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
      if (token.kind === TokenKind.Exclamation && !token.precededByLineBreak) {
        this.nextToken();
        expression = { kind: SyntaxKind.NonNullExpression, expression, start: expression.start, end: token.end };
        continue;
      }
      return expression;
    }
  },

  previousEnd(this: Parser, args: readonly Expression[], fallback: number): number {
    return args[args.length - 1]?.end ?? fallback;
  },

  parsePropertyAccess(this: Parser, expression: Expression): Expression {
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
    const name = this.parseMemberName();
    return { kind: SyntaxKind.PropertyAccessExpression, expression, name, optional, start: expression.start, end: name.end };
  },

  /** Member name after `.`: a normal identifier or a `#private` name. */
  parseMemberName(this: Parser): Identifier {
    const token = this.token;
    if (token.kind === TokenKind.PrivateIdentifier) {
      this.nextToken();
      return { kind: SyntaxKind.Identifier, text: token.text, escaped: true, start: token.start, end: token.end };
    }
    return this.parseIdentifierName();
  },

  parseArguments(this: Parser): Expression[] {
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
  },

};
