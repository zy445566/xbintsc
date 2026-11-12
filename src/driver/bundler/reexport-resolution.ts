/**
 * Resolve re-export chains and rebuild namespace objects from final names.
 *
 * Phase 2 records `export { x } from "./dep"` by copying the dependency's
 * export *value* at that moment; if `dep` re-exports an import, that value is
 * the pre-rename local name and is only corrected by Phase 3's import rewrite.
 * A multi-level chain (`index -> sdk -> tools`) therefore needs a second pass,
 * and a namespace import reaching back across a cycle needs the final names.
 * This pass re-resolves module re-exports until the export maps stop changing,
 * then rebuilds every eager namespace object from those final names.
 *
 * Deferred modules (`<script src>`) are the exception: their bindings live in a
 * wrapper function, so Phase 3 already lowered any namespace binding between
 * two deferred modules to an alias that calls `__xt_deferred_<dep>()`. Such a
 * binding must be left alone; rebuilding it here would reference wrapper-local
 * names from the top level (TS3002 `Cannot find name`).
 */

import {
  SyntaxKind,
  type ExportDeclaration,
  type Identifier,
  type ImportDeclaration,
  type Statement,
} from "../../ast/nodes.js";
import type { ModuleRecord } from "./types.js";
import { namespaceImportToStatement } from "./namespace-object.js";

export function resolveReexports(
  records: ModuleRecord[],
  dependencyOf: (record: ModuleRecord, specifier: string) => ModuleRecord | undefined,
  namespaceStatements: Map<Statement, Statement>,
  deferredModules: ReadonlySet<string> = new Set(),
): void {
  let changed = true;
  // A re-export chain cannot be longer than the module count, so this bound
  // guarantees termination even for a cyclic `export *` between two modules.
  for (let iteration = 0; iteration <= records.length && changed; iteration++) {
    changed = false;
    for (const record of records) {
      for (const statement of record.sourceFile.statements) {
        if (statement.kind !== SyntaxKind.ExportDeclaration) continue;
        const declaration = statement as ExportDeclaration;
        if (declaration.isTypeOnly || !declaration.moduleSpecifier) continue;
        const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
        if (!dependency) continue;
        /* Phase 3 (the merge pass) already lowered a deferred module's re-export
         * of another deferred module to a wrapper alias that reads the exported
         * value from `__xt_deferred_<dep>()`. Re-resolving that alias here would
         * point it back at the dependency's final name, which is wrapper-local
         * and no longer declared at top level (TS3002). */
        if (deferredModules.has(record.path) && deferredModules.has(dependency.path)) continue;
        const clause = declaration.exportClause;
        if (clause && clause.kind === SyntaxKind.NamedExports) {
          for (const specifier of clause.elements) {
            if (specifier.isTypeOnly) continue;
            const localName = specifier.propertyName?.text ?? specifier.name.text;
            const target = dependency.exports.get(localName);
            if (target && record.exports.get(specifier.name.text) !== target) {
              record.exports.set(specifier.name.text, target);
              changed = true;
            }
          }
        } else if (!clause) {
          for (const [name, value] of dependency.exports) {
            if (record.exports.get(name) !== value) {
              record.exports.set(name, value);
              changed = true;
            }
          }
        }
      }
    }
  }
  // Rebuild namespace objects now that every export name is final.
  for (const record of records) {
    for (const statement of record.sourceFile.statements) {
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        if (declaration.isTypeOnly || !declaration.moduleSpecifier) continue;
        if (declaration.exportClause?.kind === SyntaxKind.NamespaceImport) {
          const nsName = (declaration.exportClause as { name: Identifier }).name.text;
          const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
          if (dependency) {
            namespaceStatements.set(
              statement,
              namespaceImportToStatement(statement, `${record.prefix}${nsName}`, dependency),
            );
          }
        }
      } else if (statement.kind === SyntaxKind.ImportDeclaration) {
        const declaration = statement as ImportDeclaration;
        const clause = declaration.importClause;
        if (clause?.isTypeOnly) continue;
        const bindings = clause?.namedBindings;
        if (bindings?.kind === SyntaxKind.NamespaceImport) {
          const dependency = dependencyOf(record, declaration.moduleSpecifier.value);
          /* Phase 3 already lowered a deferred module's namespace import of
           * another deferred module to an alias read from the dependency's
           * wrapper. Rebuilding the object here would reference the
           * dependency's final names, which are wrapper-local (TS3002). */
          if (dependency && !(deferredModules.has(record.path) && deferredModules.has(dependency.path))) {
            namespaceStatements.set(
              statement,
              namespaceImportToStatement(statement, `${record.prefix}${bindings.name.text}`, dependency),
            );
          }
        }
      }
    }
  }
}
