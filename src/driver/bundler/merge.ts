/**
 * Bundling pass orchestrator (phases 1-3).
 *
 * Dependencies come first (post-order over the import graph), the entry module
 * last. These passes record each module's exports, pick globally unique final
 * names, rewrite imported names and lower deferred imports. `concatenate.ts`
 * then emits the statement list and applies the final renames (phases 4-5).
 */

import { dirname } from "node:path";
import {
  ModifierKind,
  SyntaxKind,
  type ExportDeclaration,
  type Identifier,
  type ImportDeclaration,
  type Node,
  type Statement,
  type VariableStatement,
} from "../../ast/nodes.js";
import type { BindingName } from "../../ast/declarations.js";
import { SymbolKind } from "../../binder/binder.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import type { AssetLoader } from "../../extensions/registry.js";
import { concatenateModules } from "./concatenate.js";
import { lowerCommonJSRequires, prepareCommonJS } from "./commonjs.js";
import {
  computeDeferredModules,
  deferredAliasName,
  deferredWrapperName,
  isDeferredMarkerImport,
  type DeferredAlias,
} from "./deferred.js";
import { prepareDynamicImports } from "./dynamic-import.js";
import { loadGraph } from "./graph.js";
import { namespaceImportToStatement } from "./namespace-object.js";
import { resolveReexports } from "./reexport-resolution.js";
import { classifyDependency } from "./resolve.js";
import {
  declarationName,
  hasModifier,
  moduleScope,
  renameImport,
  renameReferences,
  topLevelSymbols,
} from "./symbols.js";
import type { BundleResult, ModuleRecord } from "./types.js";

export function bundleModules(
  entryPath: string,
  diagnostics: DiagnosticBag,
  externalSpecifiers: ReadonlySet<string> = new Set(),
  assetLoaders: Readonly<Record<string, AssetLoader>> = {},
  moduleOverrides?: Readonly<Record<string, string>>,
): BundleResult | undefined {
  const records = loadGraph(entryPath, diagnostics, externalSpecifiers, assetLoaders, moduleOverrides);
  if (!records) return undefined;

  const byPath = new Map(records.map((record) => [record.path, record]));
  const dependencyOf = (record: ModuleRecord, specifier: string): ModuleRecord | undefined => {
    const resolution = classifyDependency(dirname(record.path), specifier, externalSpecifiers, moduleOverrides);
    return resolution.kind === "file" ? byPath.get(resolution.path) : undefined;
  };

  // Rewrite dynamic imports and record modules reached only through them.
  const dynamicTargets = prepareDynamicImports(records, dependencyOf);

  // Phase 1: record every top-level binding and pick a unique final name.
  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      record.originalNames.set(symbol.id, symbol.name);
      if (symbol.kind === SymbolKind.Import) continue;
      record.finalNames.set(symbol.id, record.prefix + symbol.name);
    }
  }

  // Phase 1b: give every CommonJS module its `module`/`exports` state and
  // record its export surface, so later phases can re-export it.
  for (const record of records) prepareCommonJS(record);

  // Phase 2: collect each module's exported names.
  const namespaceStatements = new Map<Statement, Statement>();
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    const symbolByName = scope.symbols;
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        if (declaration.isTypeOnly) continue;
        if (declaration.exportClause && declaration.exportClause.kind === SyntaxKind.NamedExports) {
          for (const specifier of declaration.exportClause.elements) {
            if (specifier.isTypeOnly) continue;
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
        } else if (declaration.exportClause?.kind === SyntaxKind.NamespaceImport && declaration.moduleSpecifier) {
          // `export * as ns from "..."`: re-export the dependency's namespace.
          const nsName = (declaration.exportClause as { name: Identifier }).name.text;
          const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
          if (dependency) {
            const finalName = `${record.prefix}${nsName}`;
            record.exports.set(nsName, finalName);
            namespaceStatements.set(statement, namespaceImportToStatement(statement, finalName, dependency));
          }
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

  // Phase 2b: resolve `require("...")` calls now that every module's export
  // surface is known (a required ESM module becomes a namespace object).
  for (const record of records) {
    lowerCommonJSRequires(record, {
      resolve: (fromDir, specifier) => classifyDependency(fromDir, specifier, externalSpecifiers, moduleOverrides),
      recordFor: (path) => byPath.get(path),
      diagnostics,
    });
  }

  // Phase 3: rewrite imported names and record re-exports. Namespace imports
  // are lowered to a synthetic object literal holding every export, built
  // from the dependency's (already renamed) final names.
  const deferredModules = computeDeferredModules(records, externalSpecifiers, moduleOverrides);
  const deferredAliases = new Map<string, DeferredAlias[]>();
  const markerTargets = new Set<string>();
  for (const record of records) {
    const scope = moduleScope(record);
    if (!scope) continue;
    const deferred = deferredModules.has(record.path);
    const aliases: DeferredAlias[] = [];
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        if (!deferred) continue;
        const declaration = statement as ExportDeclaration;
        if (!declaration.moduleSpecifier) continue;
        const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
        if (!dependency || !deferredModules.has(dependency.path)) continue;
        const wrapper = deferredWrapperName(dependency);
        const clause = declaration.exportClause;
        if (clause && clause.kind === SyntaxKind.NamedExports) {
          for (const specifier of clause.elements) {
            if (specifier.isTypeOnly) continue;
            const importedName = specifier.propertyName?.text ?? specifier.name.text;
            const alias = deferredAliasName(record, aliases.length, `re_${specifier.name.text}`);
            aliases.push({
              name: alias,
              wrapper,
              importedName,
              start: declaration.start,
              end: declaration.end,
            });
            record.exports.set(specifier.name.text, alias);
          }
        } else if (!clause) {
          /* `export * from "./dep"`: alias every runtime export the deferred
           * dependency contributes, so our own wrapper can return them. */
          for (const [name, target] of dependency.exports) {
            if (!target) continue;
            const alias = deferredAliasName(record, aliases.length, `re_${name}`);
            aliases.push({
              name: alias,
              wrapper,
              importedName: name,
              start: declaration.start,
              end: declaration.end,
            });
            record.exports.set(name, alias);
          }
        }
        continue;
      }
      if (statement.kind !== SyntaxKind.ImportDeclaration) continue;
      const declaration = statement as ImportDeclaration;
      const clause = declaration.importClause;
      if (clause?.isTypeOnly) continue;
      const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
      if (!dependency) continue;
      const wrapper = deferredWrapperName(dependency);

      /* `import "./side-effect"`: a deferred module still has to run the
       * dependency's top-level code when it initialises. */
      if (!clause) {
        if (deferred && deferredModules.has(dependency.path)) {
          aliases.push({
            name: "",
            wrapper,
            sideEffect: true,
            start: declaration.start,
            end: declaration.end,
          });
        }
        continue;
      }

      if (isDeferredMarkerImport(declaration)) {
        /* `<script src>`'s deferred imports: the loader calls the binding as a
         * function returning the dependency's exports. */
        markerTargets.add(dependency.path);
        const bindings = clause.namedBindings;
        if (bindings && bindings.kind === SyntaxKind.NamedImports) {
          for (const specifier of bindings.elements) {
            const local = scope.symbols.get(specifier.name.text);
            if (local) renameReferences(local, wrapper);
          }
        }
        continue;
      }

      if (deferred && deferredModules.has(dependency.path)) {
        /* Imports inside a deferred module become local aliases read from the
         * dependency's wrapper when it initialises, not global names. */
        const start = declaration.start;
        const end = declaration.end;
        if (clause.name) {
          const local = scope.symbols.get(clause.name.text);
          const alias = deferredAliasName(record, aliases.length, "default");
          if (local) renameReferences(local, alias);
          aliases.push({
            name: alias,
            wrapper,
            importedName: "default",
            local: clause.name.text,
            start,
            end,
          });
        }
        const bindings = clause.namedBindings;
        if (bindings && bindings.kind === SyntaxKind.NamedImports) {
          if (bindings.elements.length === 0) {
            aliases.push({ name: "", wrapper, sideEffect: true, start, end });
          }
          for (const specifier of bindings.elements) {
            if (specifier.isTypeOnly) continue;
            const importedName = specifier.propertyName?.text ?? specifier.name.text;
            const local = scope.symbols.get(specifier.name.text);
            const alias = deferredAliasName(record, aliases.length, importedName);
            if (local) renameReferences(local, alias);
            aliases.push({
              name: alias,
              wrapper,
              importedName,
              local: specifier.name.text,
              start,
              end,
            });
          }
        } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
          const local = scope.symbols.get(bindings.name.text);
          const alias = deferredAliasName(record, aliases.length, "namespace");
          if (local) renameReferences(local, alias);
          aliases.push({
            name: alias,
            wrapper,
            namespace: true,
            local: bindings.name.text,
            start,
            end,
          });
        }
        continue;
      }

      if (clause.name) {
        const target = dependency.exports.get("default");
        const local = scope.symbols.get(clause.name.text);
        if (target && local) renameImport(record, local, target);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          if (specifier.isTypeOnly) continue;
          const importedName = specifier.propertyName?.text ?? specifier.name.text;
          const target = dependency.exports.get(importedName);
          const local = scope.symbols.get(specifier.name.text);
          if (target && local) renameImport(record, local, target);
        }
      } else if (bindings && bindings.kind === SyntaxKind.NamespaceImport) {
        const local = scope.symbols.get(bindings.name.text);
        const finalName = `${record.prefix}${bindings.name.text}`;
        if (local) renameReferences(local, finalName);
        namespaceStatements.set(statement, namespaceImportToStatement(statement, finalName, dependency));
      }
    }
    if (aliases.length > 0) {
      deferredAliases.set(record.path, aliases);
      /* `import { foo } from "./x"; export { foo };` maps the export to the
       * import's local name in Phase 2; the wrapper's binding is the alias. */
      const localAliases = new Map<string, string>();
      for (const alias of aliases) {
        if (alias.local !== undefined && alias.name.length > 0) {
          localAliases.set(alias.local, alias.name);
        }
      }
      if (localAliases.size > 0) {
        for (const [name, local] of record.exports) {
          const alias = localAliases.get(local);
          if (alias !== undefined) record.exports.set(name, alias);
        }
      }
    }
  }

  // Phase 3b: re-resolve module re-exports and rebuild namespace objects with
  // the final renamed names (multi-level re-export chains and cycle back-edges).
  resolveReexports(records, dependencyOf, namespaceStatements, deferredModules);

  // Phases 4-5: concatenate the modules into one file and give every top-level
  // binding its final name.
  const sourceFile = concatenateModules({
    records,
    deferredModules,
    deferredAliases,
    markerTargets,
    dynamicTargets,
    namespaceStatements,
    externalSpecifiers,
    moduleOverrides,
  });
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
  for (const element of name.elements) {
    if (element) collectBindingIdentifiers(element.name, out);
  }
}
