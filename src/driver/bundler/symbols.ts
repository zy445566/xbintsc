/**
 * Top-level symbol helpers: name extraction and declaration/reference
 * renaming used while merging modules into a single file.
 */

import { ModifierKind, SyntaxKind, type Identifier, type Node, type VariableDeclaration } from "../../ast/nodes.js";
import type { SymbolInfo } from "../../binder/binder.js";
import type { ModuleRecord } from "./types.js";

export function hasModifier(node: Node, kind: ModifierKind): boolean {
  const modifiers = (node as { modifiers?: { modifierKind: ModifierKind }[] }).modifiers;
  return !!modifiers && modifiers.some((modifier) => modifier.modifierKind === kind);
}

export function declarationName(node: Node): Identifier | undefined {
  switch (node.kind) {
    /* Destructured bindings are declared with their identifier node (see
     * `Binder.declareBinding`), so renaming must accept a bare Identifier too;
     * otherwise the declaration keeps its old name while references are
     * rewritten and the merged program fails to resolve it. */
    case SyntaxKind.Identifier:
      return node as Identifier;
    case SyntaxKind.FunctionDeclaration:
    case SyntaxKind.ClassDeclaration:
    case SyntaxKind.EnumDeclaration:
    case SyntaxKind.InterfaceDeclaration:
    case SyntaxKind.TypeAliasDeclaration:
    case SyntaxKind.ModuleDeclaration:
      return (node as { name?: Identifier }).name;
    case SyntaxKind.VariableDeclaration: {
      const name = (node as VariableDeclaration).name;
      return name.kind === SyntaxKind.Identifier ? name : undefined;
    }
    default:
      return undefined;
  }
}

/** Rewrite a symbol's declaration and every reference to `finalName`. */
export function renameSymbol(symbol: SymbolInfo, finalName: string): void {
  for (const declaration of symbol.declarations) {
    const name = declarationName(declaration);
    if (name) (name as { text: string }).text = finalName;
  }
  for (const reference of symbol.references) (reference as { text: string }).text = finalName;
}

/** Rewrite just the references of a symbol (used for imported names). */
export function renameReferences(symbol: SymbolInfo, finalName: string): void {
  for (const reference of symbol.references) (reference as { text: string }).text = finalName;
}

/** `export { foo }` where `foo` is imported: point the export at the renamed target. */
export function remapExports(record: ModuleRecord, fromName: string, toName: string): void {
  for (const [name, value] of record.exports) {
    if (value === fromName) record.exports.set(name, toName);
  }
}

/** Rename an imported binding and fix any `export { foo }` that re-exports it. */
export function renameImport(record: ModuleRecord, symbol: SymbolInfo, target: string): void {
  renameReferences(symbol, target);
  remapExports(record, symbol.name, target);
}

export function moduleScope(record: ModuleRecord) {
  return record.bind.scopes.get(record.sourceFile);
}

export function topLevelSymbols(record: ModuleRecord): SymbolInfo[] {
  const scope = moduleScope(record);
  if (!scope) return [];
  return [...scope.symbols.values()];
}
