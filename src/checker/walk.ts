/**
 * A depth-first walk over the AST.
 *
 * `childNodes` is the binder's generic child accessor: it visits every field
 * that looks like a node, including type annotations and operator tokens. That
 * is fine for the checks here — they only react to a handful of expression
 * kinds, and tokens carry a string `kind` while AST nodes carry a numeric one.
 */

import type { Node } from "../ast/nodes.js";
import { childNodes } from "../binder/helpers.js";

export function walk(node: Node, visit: (node: Node) => void): void {
  visit(node);
  for (const child of childNodes(node)) walk(child, visit);
}
