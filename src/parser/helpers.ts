/**
 * Small AST-shape predicates shared by the parser's method groups.
 */

import { SyntaxKind } from "../ast/kinds.js";
import type { Expression, PropertyName } from "../ast/nodes.js";

export function expressionToPropertyName(expr: Expression): PropertyName | undefined {
  switch (expr.kind) {
    case SyntaxKind.StringLiteral:
      return expr;
    case SyntaxKind.NumericLiteral:
      return expr;
    case SyntaxKind.Identifier:
      return expr;
    case SyntaxKind.PropertyAccessExpression:
      return expr.name;
    default:
      return undefined;
  }
}

export function propertyNameText(name: PropertyName): string {
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

export function isAssignmentTarget(expr: Expression): boolean {
  switch (expr.kind) {
    case SyntaxKind.Identifier:
    case SyntaxKind.PropertyAccessExpression:
    case SyntaxKind.ElementAccessExpression:
    case SyntaxKind.ParenthesizedExpression:
    // Array/object literals are valid on the left of `=` as destructuring
    // patterns (`[a, b] = xs`, `({ a } = obj)`).
    case SyntaxKind.ArrayLiteralExpression:
    case SyntaxKind.ObjectLiteralExpression:
      return true;
    default:
      return false;
  }
}
