/**
 * xbintsc public API surface.
 *
 * Re-exports the compiler pipeline so the CLI, tests and embedding tools can
 * compose stages (lex, parse, bind, generate) without depending on internals.
 */

export { SourceFile, hashText, combineHashes } from "./diagnostics/source.js";
export {
  DiagnosticBag,
  DiagnosticCode,
  DiagnosticError,
  formatDiagnostic,
  formatDiagnostics,
  type Diagnostic,
} from "./diagnostics/diagnostic.js";
export { Scanner } from "./lexer/scanner.js";
export { TokenKind, KEYWORDS, type Token } from "./lexer/token.js";
export * from "./ast/nodes.js";
export { Parser } from "./parser/parser.js";
export { bind, SymbolKind, ScopeKind, type BindResult, type FunctionInfo, type SymbolInfo } from "./binder/binder.js";
export { generate, type CodegenResult, type CodegenOptions } from "./codegen/llvm.js";
export {
  XT_UNDEFINED,
  XT_NULL,
  XT_TRUE,
  XT_FALSE,
  numberLiteral,
  booleanLiteral,
} from "./codegen/values.js";
export { ExtensionRegistry, createDefaultRegistry, coreExtension, type Extension, type ExtensionModule, type ModuleExport, type ModuleExports } from "./extensions/registry.js";
export { nodeExtension } from "./extensions/node/index.js";
export type { NodeModule } from "./extensions/node/module.js";
export * from "./driver/index.js";
