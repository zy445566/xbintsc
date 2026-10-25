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

/**
 * True when a node came from a plain JavaScript file (`.js`, `.mjs`, `.cjs`,
 * `.jsx`).
 *
 * A JavaScript program is untyped by definition: TypeScript only infers and
 * reports on such files with `checkJs`, and xbintsc honours the same boundary.
 * Inferring types from unannotated JavaScript rejects valid code — a flag
 * assigned `false` then `true` narrows to `false`, a method installed on a
 * state object after its literal (`Game.foo = …`) is not a property of the
 * literal, a closure variable initialized to `null` is "not callable", a call
 * that relies on default parameters looks like an arity error. Names are still
 * resolved (that is what code generation needs), and redeclarations are still
 * reported, but no type, arity or property diagnostic is produced.
 */
export function isPlainJavaScriptNode(sourceFile: SourceFileNode, node: Node): boolean {
  const origin = sourceFile.origins?.get(node) ?? sourceFile.fileName;
  return /\.(js|mjs|cjs|jsx)$/i.test(origin);
}

/** True when a node is exempt from type checking: plain JavaScript or a
 * bundled dependency (see {@link isDependencyNode}). */
export function isUntypedNode(sourceFile: SourceFileNode, node: Node): boolean {
  return isDependencyNode(sourceFile, node) || isPlainJavaScriptNode(sourceFile, node);
}
