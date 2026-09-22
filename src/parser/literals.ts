/**
 * Literal and "primary aggregate" expression parsing: array/object literals,
 * function/class expressions, template literals and regular expression
 * literals.
 */

import { DiagnosticCode } from "../diagnostics/diagnostic.js";
import { TokenKind } from "../lexer/token.js";
import {
  NodeFlags,
  SyntaxKind,
  type ArrayLiteralExpression,
  type ClassExpression,
  type Expression,
  type FunctionExpression,
  type Identifier,
  type ObjectLiteralElementLike,
  type ObjectLiteralExpression,
  type RegularExpressionLiteral,
  type ShorthandPropertyAssignment,
  type TemplateLiteral,
  type TemplateSpan,
  type TypeNode,
} from "../ast/nodes.js";
import type { Parser } from "./parser.js";

export interface LiteralMethods {
  parseArrayLiteral(this: Parser): ArrayLiteralExpression;
  parseObjectLiteral(this: Parser): ObjectLiteralExpression;
  parseFunctionExpression(this: Parser): FunctionExpression;
  parseClassExpression(this: Parser): ClassExpression;
  parseTemplateLiteral(this: Parser): TemplateLiteral;
  parseRegularExpression(this: Parser): RegularExpressionLiteral;
}

/** Strip the delimiters from a scanned template chunk and normalize newlines. */
function rawTemplateChunk(text: string, leading: number, trailing: number): string {
  const end = Math.max(leading, text.length - trailing);
  return text.slice(leading, end).replace(/\r\n?/g, "\n");
}

export const literalMethods: LiteralMethods = {
  parseArrayLiteral(this: Parser): ArrayLiteralExpression {
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
  },

  parseObjectLiteral(this: Parser): ObjectLiteralExpression {
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
          // `{ a }` shorthand, optionally with a default (`{ a = 1 }`) that is
          // only meaningful when the object is a destructuring assignment target.
          let initializer: Expression | undefined;
          let end = name.end;
          if (this.at(TokenKind.Equals)) {
            this.nextToken();
            initializer = this.parseAssignmentExpression();
            end = initializer.end;
          }
          const shorthand: ShorthandPropertyAssignment = { kind: SyntaxKind.ShorthandPropertyAssignment, name, initializer, start, end };
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
  },

  parseFunctionExpression(this: Parser): FunctionExpression {
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
  },

  parseClassExpression(this: Parser): ClassExpression {
    const start = this.parseExpected(TokenKind.ClassKeyword).start;
    let name: Identifier | undefined;
    if (this.at(TokenKind.Identifier)) name = this.parseIdentifier();
    const typeParameters = this.parseTypeParameters();
    const heritage = this.parseHeritageClauses();
    const members = this.parseClassMembers();
    return { kind: SyntaxKind.ClassExpression, name, typeParameters, heritage, members, start, end: members[members.length - 1]?.end ?? start };
  },

  parseTemplateLiteral(this: Parser): TemplateLiteral {
    const startToken = this.token;
    if (startToken.kind === TokenKind.NoSubstitutionTemplateLiteral) {
      this.nextToken();
      return {
        kind: SyntaxKind.NoSubstitutionTemplateLiteral,
        text: startToken.text,
        value: String(startToken.value ?? ""),
        raw: rawTemplateChunk(startToken.text, 1, 1),
        start: startToken.start,
        end: startToken.end,
      } as unknown as TemplateLiteral;
    }
    if (startToken.kind === TokenKind.Backtick) {
      // Re-scan from the backtick so the scanner produces a TemplateHead.
      this.scanner.seek(startToken.start);
      this.tokens = [this.scanner.nextToken()];
      this.index = 0;
      return this.parseTemplateLiteral();
    }
    const head = String(startToken.value ?? "");
    const headRaw = rawTemplateChunk(startToken.text, 1, 2);
    this.nextToken();
    const spans: TemplateSpan[] = [];
    for (;;) {
      const expression = this.parseExpression();
      if (!this.at(TokenKind.CloseBrace)) {
        this.error(DiagnosticCode.UnterminatedTemplate, "Expected '}' to close template substitution");
        return { kind: SyntaxKind.TemplateLiteral, head, raw: headRaw, spans, start: startToken.start, end: this.token.end };
      }
      // The `}` terminates a substitution; re-read it as template text without
      // first scanning the following (template) characters as normal tokens.
      const closeStart = this.token.start;
      this.tokens = [this.scanner.continueTemplate(closeStart)];
      this.index = 0;
      const literalToken = this.token;
      if (literalToken.kind === TokenKind.NoSubstitutionTemplateLiteral) {
        this.nextToken();
        spans.push({
          kind: SyntaxKind.TemplateSpan,
          expression,
          literal: String(literalToken.value ?? ""),
          raw: rawTemplateChunk(literalToken.text, 0, 1),
          isTail: true,
          start: expression.start,
          end: literalToken.end,
        });
        return { kind: SyntaxKind.TemplateLiteral, head, raw: headRaw, spans, start: startToken.start, end: literalToken.end };
      }
      if (literalToken.kind === TokenKind.TemplateHead) {
        this.nextToken();
        spans.push({
          kind: SyntaxKind.TemplateSpan,
          expression,
          literal: String(literalToken.value ?? ""),
          raw: rawTemplateChunk(literalToken.text, 0, 2),
          isTail: false,
          start: expression.start,
          end: literalToken.end,
        });
        continue;
      }
      this.error(DiagnosticCode.UnterminatedTemplate, "Unterminated template literal", literalToken);
      return { kind: SyntaxKind.TemplateLiteral, head, raw: headRaw, spans, start: startToken.start, end: literalToken.end };
    }
  },

  parseRegularExpression(this: Parser): RegularExpressionLiteral {
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
  },
};
