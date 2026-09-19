import { SyntaxKind, type Node } from "./nodes.js";

/**
 * A generic, allocation-light AST visitor. The parser, binder and checker all
 * traverse the tree; centralizing the "what are my children" logic avoids the
 * classic bug where a new node kind is added but one traversal forgets it.
 */

export type VisitorFn<T extends Node = Node> = (node: T) => void;

export interface Visitor {
  [SyntaxKind.Identifier]?: VisitorFn;
  [key: number]: VisitorFn | undefined;
}

/** Return the direct children of a node for generic traversal. */
export function getChildren(node: Node): Node[] {
  const children: Node[] = [];
  const push = (n: unknown): void => {
    if (n && typeof n === "object" && "kind" in (n as Node)) children.push(n as Node);
  };
  const pushAll = (list: readonly unknown[] | undefined): void => {
    if (!list) return;
    for (const item of list) push(item);
  };

  const n = node as unknown as Record<string, unknown>;
  for (const key of Object.keys(n)) {
    if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value") continue;
    const value = n[key];
    if (Array.isArray(value)) pushAll(value);
    else if (value && typeof value === "object" && "kind" in (value as object)) push(value);
  }
  return children;
}

/** Depth-first pre-order traversal. */
export function forEachChild(node: Node, visit: (child: Node) => void): void {
  for (const child of getChildren(node)) visit(child);
}

export function walk(node: Node, visit: VisitorFn): void {
  visit(node);
  for (const child of getChildren(node)) walk(child, visit);
}

/** Find the innermost node whose range contains `offset`. */
export function nodeAtPosition(root: Node, offset: number): Node | undefined {
  let current: Node | undefined = root;
  let result: Node | undefined;
  while (current) {
    const children: Node[] = getChildren(current).filter((c: Node) => c.start <= offset && offset < c.end);
    if (children.length === 0) break;
    // Prefer the smallest (innermost) child.
    children.sort((a: Node, b: Node) => a.end - a.start - (b.end - b.start));
    current = children[0];
    result = current;
  }
  return result;
}
