/**
 * Small AST helpers shared by the binder's declaration and reference passes.
 */

import { SyntaxKind, type Node, type PropertyName } from "../ast/nodes.js";
import { SymbolKind } from "./types.js";

/** Direct AST children, tolerant of both arrays and single nodes. */
export function childNodes(node: Node): Node[] {
  const result: Node[] = [];
  const n = node as unknown as Record<string, unknown>;
  for (const key of Object.keys(n)) {
    if (key === "kind" || key === "start" || key === "end") continue;
    const value = n[key];
    if (Array.isArray(value)) {
      for (const item of value) if (item && typeof item === "object" && "kind" in (item as object)) result.push(item as Node);
    } else if (value && typeof value === "object" && "kind" in (value as object)) {
      result.push(value as Node);
    }
  }
  return result;
}

/** Only value bindings participate in closure capture. */
export function isCapturable(kind: SymbolKind): boolean {
  return (
    kind === SymbolKind.Var ||
    kind === SymbolKind.Let ||
    kind === SymbolKind.Const ||
    kind === SymbolKind.Parameter
  );
}

/** Extract the textual name of a class member (`method`, `"key"`, `0`). */
export function classMemberName(name: PropertyName): string {
  switch (name.kind) {
    case SyntaxKind.Identifier:
    case SyntaxKind.PrivateIdentifier:
      return (name as unknown as { text: string }).text;
    case SyntaxKind.StringLiteral:
      return (name as unknown as { value: string }).value;
    case SyntaxKind.NumericLiteral:
      return String((name as unknown as { value: number }).value);
    default:
      return "";
  }
}
