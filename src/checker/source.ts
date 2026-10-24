/**
 * Small source-location helpers shared by the checker.
 */

import type { Node, SourceFileNode } from "../ast/nodes.js";

/**
 * The file a node came from. Bundling merges every module into one
 * `SourceFile` while keeping module-local offsets, so the origin map is what
 * keeps a diagnostic pointing at the file the user actually wrote.
 */
export function fileNameOf(sourceFile: SourceFileNode, node: Node): string {
  return sourceFile.origins?.get(node) ?? sourceFile.fileName;
}

/**
 * True when a node came from a bundled package under `node_modules`.
 *
 * The checker is a gate for the code the user wrote, not for dependency
 * internals: a package may legitimately use `globalThis`, call a function
 * through a pattern the arity heuristic cannot see, or rely on its own
 * transpiler's output. Reporting those would make enabling the checker break
 * otherwise working builds.
 */
export function isDependencyNode(sourceFile: SourceFileNode, node: Node): boolean {
  const origin = sourceFile.origins?.get(node);
  return origin !== undefined && /(^|[\\/])node_modules[\\/]/.test(origin);
}
