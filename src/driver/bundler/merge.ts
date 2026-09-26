/**
 * Merge every reachable module's statements into a single source file.
 *
 * Dependencies come first (post-order over the import graph), the entry module
 * last. Top-level bindings are renamed to globally unique names and imports are
 * rewritten to the (renamed) exported binding; namespace imports are lowered
 * to a synthetic object literal.
 */

import { dirname } from "node:path";
import {
  ModifierKind,
  SyntaxKind,
  type ExportAssignment,
  type ExportDeclaration,
  type Identifier,
  type ImportDeclaration,
  type Node,
  type ObjectLiteralExpression,
  type PropertyAssignment,
  type SourceFileNode,
  type Statement,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../../ast/nodes.js";
import type { BindingName } from "../../ast/declarations.js";
import { SymbolKind } from "../../binder/binder.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { loadGraph } from "./graph.js";
import { classifyDependency } from "./resolve.js";
import {
  declarationName,
  hasModifier,
  moduleScope,
  renameReferences,
  renameSymbol,
  topLevelSymbols,
} from "./symbols.js";
import type { BundleResult, ModuleRecord } from "./types.js";

export function bundleModules(
  entryPath: string,
  diagnostics: DiagnosticBag,
  externalSpecifiers: ReadonlySet<string> = new Set(),
): BundleResult | undefined {
  const records = loadGraph(entryPath, diagnostics, externalSpecifiers);
  if (!records) return undefined;

  const byPath = new Map(records.map((record) => [record.path, record]));
  const dependencyOf = (record: ModuleRecord, specifier: string): ModuleRecord | undefined => {
    const resolution = classifyDependency(dirname(record.path), specifier, externalSpecifiers);
    return resolution.kind === "file" ? byPath.get(resolution.path) : undefined;
  };

  // Phase 1: record every top-level binding and pick a unique final name.
  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      record.originalNames.set(symbol.id, symbol.name);
      if (symbol.kind === SymbolKind.Import) continue;
      record.finalNames.set(symbol.id, record.prefix + symbol.name);
    }
  }

  // Phase 2: collect each module's exported names.
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    const symbolByName = scope.symbols;
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        if (declaration.exportClause && declaration.exportClause.kind === SyntaxKind.NamedExports) {
          for (const specifier of declaration.exportClause.elements) {
            const localName = specifier.propertyName?.text ?? specifier.name.text;
            const exportedName = specifier.name.text;
            if (declaration.moduleSpecifier) {
              const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
              const target = dependency?.exports.get(localName);
              if (target) record.exports.set(exportedName, target);
            } else {
              const symbol = symbolByName.get(localName);
              if (symbol) {
                const finalName = record.finalNames.get(symbol.id) ?? symbol.name;
                record.exports.set(exportedName, finalName);
              }
            }
          }
        } else if (!declaration.exportClause && declaration.moduleSpecifier) {
          // `export * from "..."`: copy every re-exported name.
          const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
          if (dependency) for (const [name, value] of dependency.exports) record.exports.set(name, value);
        }
        continue;
      }
      if (statement.kind === SyntaxKind.ExportAssignment) {
        record.exports.set("default", `${record.prefix}default`);
        continue;
      }
      if (hasModifier(statement, ModifierKind.Default)) {
        const name = declarationName(statement) ?? declarationName((statement as { declaration?: Node }).declaration ?? statement);
        if (name) {
          const symbol = symbolByName.get(name.text);
          if (symbol) record.exports.set("default", record.finalNames.get(symbol.id) ?? symbol.name);
        }
        continue;
      }
      if (hasModifier(statement, ModifierKind.Export)) {
        const names = exportedNames(statement);
        for (const name of names) {
          const symbol = symbolByName.get(name);
          if (symbol) record.exports.set(name, record.finalNames.get(symbol.id) ?? symbol.name);
        }
      }
    }
  }

  // Phase 3: rewrite imported names and record re-exports. Namespace imports
  // are lowered to a synthetic object literal holding every export, built
  // from the dependency's (already renamed) final names.
  const namespaceStatements = new Map<Statement, Statement>();
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    for (const statement of record.sourceFile.statements) {
      if (statement.kind !== SyntaxKind.ImportDeclaration) continue;
      const declaration = statement as ImportDeclaration;
      const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
      const clause = declaration.importClause;
      if (!clause || !dependency) continue;
      if (clause.name) {
        const target = dependency.exports.get("default") ?? dependency.exports.get("default");
        const local = scope.symbols.get(clause.name.text);
        if (target && local) renameReferences(local, target);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          const importedName = specifier.propertyName?.text ?? specifier.name.text;
          const target = dependency.exports.get(importedName);
          const local = scope.symbols.get(specifier.name.text);
          if (target && local) renameReferences(local, target);
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
        const local = scope.symbols.get(bindings.name.text);
        const finalName = `${record.prefix}${bindings.name.text}`;
        if (local) renameReferences(local, finalName);
        namespaceStatements.set(statement, namespaceImportToStatement(statement, finalName, dependency));
      }
    }
  }

  // Phase 4: concatenate statements, dropping imports and pure export wrappers.
  const merged: Statement[] = [];
  const first = records[records.length - 1]!;
  for (const record of records) {
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ImportDeclaration) {
        // External (extension / built-in) imports stay in place for codegen.
        const declaration = statement as ImportDeclaration;
        const resolution = classifyDependency(
          dirname(record.path),
          declaration.moduleSpecifier.value,
          externalSpecifiers,
        );
        if (resolution.kind === "external") {
          merged.push(statement);
        } else {
          const synthetic = namespaceStatements.get(statement);
          if (synthetic) merged.push(synthetic);
        }
        continue;
      }
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        const inner = (declaration as unknown as { declaration?: Statement }).declaration;
        if (inner) merged.push(inner);
        continue;
      }
      if (statement.kind === SyntaxKind.ExportAssignment) {
        const assignment = statement as ExportAssignment;
        merged.push(exportAssignmentToStatement(record, assignment));
        continue;
      }
      merged.push(statement);
    }
  }

  // Phase 5: rename every top-level binding (declaration + references) to its
  // unique final name, now that import references have been rewritten.
  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      if (symbol.kind === SymbolKind.Import) continue;
      const finalName = record.finalNames.get(symbol.id);
      if (finalName) renameSymbol(symbol, finalName);
    }
  }

  const sourceFile: SourceFileNode = {
    kind: SyntaxKind.SourceFile,
    statements: merged,
    fileName: first.path,
    text: records.map((record) => record.text).join("\n"),
    start: 0,
    end: 0,
  };
  return { sourceFile, text: sourceFile.text, moduleCount: records.length };
}

function exportedNames(statement: Statement): string[] {
  const names: string[] = [];
  if (statement.kind === SyntaxKind.VariableStatement) {
    for (const declaration of (statement as VariableStatement).declarationList.declarations) {
      collectBindingIdentifiers(declaration.name, names);
    }
  } else {
    const name = declarationName(statement);
    if (name) names.push(name.text);
  }
  return names;
}

function collectBindingIdentifiers(name: BindingName, out: string[]): void {
  if (name.kind === SyntaxKind.Identifier) {
    out.push(name.text);
    return;
  }
  const elements = name.kind === SyntaxKind.ArrayBindingPattern ? name.elements : name.elements;
  for (const element of elements) {
    if (element) collectBindingIdentifiers(element.name, out);
  }
}

/** Lower `import * as ns from "./mod"` to `const m0_ns = { ...exports }`. */
function namespaceImportToStatement(
  statement: Statement,
  finalName: string,
  dependency: ModuleRecord,
): Statement {
  const properties: PropertyAssignment[] = [];
  for (const [exportedName, target] of dependency.exports) {
    properties.push({
      kind: SyntaxKind.PropertyAssignment,
      name: { kind: SyntaxKind.Identifier, text: exportedName, start: statement.start, end: statement.start },
      initializer: { kind: SyntaxKind.Identifier, text: target, start: statement.start, end: statement.start },
      start: statement.start,
      end: statement.start,
    });
  }
  const initializer: ObjectLiteralExpression = {
    kind: SyntaxKind.ObjectLiteralExpression,
    properties,
    start: statement.start,
    end: statement.end,
  };
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name: { kind: SyntaxKind.Identifier, text: finalName, start: statement.start, end: statement.start },
    exclamation: false,
    initializer,
    start: statement.start,
    end: statement.end,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarationKind: "const" as const,
    declarations: [declaration],
    start: statement.start,
    end: statement.end,
  };
  return {
    kind: SyntaxKind.VariableStatement,
    declarationList: list,
    modifiers: [],
    start: statement.start,
    end: statement.end,
  };
}

/** Turn `export default expr` into a synthetic `const` declaration. */
function exportAssignmentToStatement(record: ModuleRecord, assignment: ExportAssignment): Statement {
  const name: Identifier = {
    kind: SyntaxKind.Identifier,
    text: `${record.prefix}default`,
    start: assignment.start,
    end: assignment.start,
  };
  record.exports.set("default", name.text);
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name,
    exclamation: false,
    initializer: assignment.expression,
    start: assignment.start,
    end: assignment.end,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarationKind: "const" as const,
    declarations: [declaration],
    start: assignment.start,
    end: assignment.end,
  };
  const statement: VariableStatement = {
    kind: SyntaxKind.VariableStatement,
    declarationList: list,
    modifiers: [],
    start: assignment.start,
    end: assignment.end,
  };
  return statement;
}
