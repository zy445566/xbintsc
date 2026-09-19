/**
 * The xbintsc abstract syntax tree.
 *
 * The AST is a faithful, lossless-enough representation of the TypeScript
 * source. Nodes are discriminated unions keyed by `kind` so the binder, checker
 * and code generator can switch exhaustively over them. Every node carries a
 * source range (`start`/`end` offsets) which the diagnostics layer maps back to
 * line/column, and which the incremental cache uses to detect changes.
 *
 * This module is the public barrel for the AST; the actual node definitions are
 * split by category into sibling files.
 */

export * from "./kinds.js";
export * from "./operators.js";
export * from "./common.js";
export * from "./expressions.js";
export * from "./statements.js";
export * from "./declarations.js";
export * from "./modules.js";
export * from "./types.js";
