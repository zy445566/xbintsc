import { SyntaxKind, type Node, type Range } from "./nodes.js";

/**
 * Small helpers used by the parser and by the code generators to build AST
 * nodes. Keeping construction in one place means the shape of every node is
 * defined exactly once and is easy to evolve.
 */

export function node<T extends Node>(value: T): T {
  return value;
}

export function range(start: number, end: number): Range {
  return { start, end };
}

/** Concatenate the ranges of several nodes into one span. */
export function span(...nodes: (Node | Range | number | undefined)[]): Range {
  let start = Number.POSITIVE_INFINITY;
  let end = Number.NEGATIVE_INFINITY;
  for (const item of nodes) {
    if (item === undefined) continue;
    if (typeof item === "number") {
      start = Math.min(start, item);
      end = Math.max(end, item);
    } else {
      start = Math.min(start, item.start);
      end = Math.max(end, item.end);
    }
  }
  if (!Number.isFinite(start)) return { start: 0, end: 0 };
  return { start, end };
}

export function isIdentifier(node: Node): node is import("./nodes.js").Identifier {
  return node.kind === SyntaxKind.Identifier;
}
