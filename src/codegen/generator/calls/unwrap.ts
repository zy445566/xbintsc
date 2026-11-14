/**
 * Small AST helpers shared by the call-access and call-invocation lowering.
 */

import { SyntaxKind, type Expression, type Identifier } from "../../../ast/nodes.js";

/** Strip `(...)`, `x as T`, `x satisfies T` and `x!` to the underlying expression. */
export function unwrapExpression(expression: Expression): Expression {
  let current = expression;
  for (;;) {
    if (
      current.kind === SyntaxKind.ParenthesizedExpression ||
      current.kind === SyntaxKind.AsExpression ||
      current.kind === SyntaxKind.SatisfiesExpression ||
      current.kind === SyntaxKind.NonNullExpression
    ) {
      current = (current as { expression: Expression }).expression;
      continue;
    }
    return current;
  }
}

/**
 * The identifier naming the global object (`globalThis`, or Node's alias
 * `global`) when `expression` is one, ignoring `(...)`/`as`/`!` wrappers.
 * Callers still have to check that the name is not shadowed by a binding.
 */
export function globalObjectReceiver(expression: Expression): Identifier | undefined {
  const receiver = unwrapExpression(expression);
  if (receiver.kind !== SyntaxKind.Identifier) return undefined;
  const name = (receiver as Identifier).text;
  return name === "globalThis" || name === "global" ? (receiver as Identifier) : undefined;
}
