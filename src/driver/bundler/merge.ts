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
  type Modifier,
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
import type { AssetLoader } from "../../extensions/registry.js";
import { loadGraph } from "./graph.js";
import { lowerCommonJSRequires, prepareCommonJS } from "./commonjs.js";
import { classifyDependency } from "./resolve.js";
import {
  computeDeferredModules,
  deferredAliasName,
  deferredModuleFunctions,
  deferredWrapperName,
  eagerModuleFunctions,
  isDeferredMarkerImport,
  type DeferredAlias,
} from "./deferred.js";
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
  assetLoaders: Readonly<Record<string, AssetLoader>> = {},
): BundleResult | undefined {
  const records = loadGraph(entryPath, diagnostics, externalSpecifiers, assetLoaders);
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

  // Phase 1b: give every CommonJS module its `module`/`exports` state and
  // record its export surface, so later phases can re-export it.
  for (const record of records) prepareCommonJS(record);

  // Phase 2: collect each module's exported names.
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
      resolve: (fromDir, specifier) => classifyDependency(fromDir, specifier, externalSpecifiers),
      recordFor: (path) => byPath.get(path),
      diagnostics,
    });
  }

  // Phase 3: rewrite imported names and record re-exports. Namespace imports
  // are lowered to a synthetic object literal holding every export, built
  // from the dependency's (already renamed) final names.
  const deferredModules = computeDeferredModules(records, externalSpecifiers);
  const deferredAliases = new Map<string, DeferredAlias[]>();
  const markerTargets = new Set<string>();
  const namespaceStatements = new Map<Statement, Statement>();
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
        const target = dependency.exports.get("default") ?? dependency.exports.get("default");
        const local = scope.symbols.get(clause.name.text);
        if (target && local) renameReferences(local, target);
      }
      const bindings = clause.namedBindings;
      if (bindings && bindings.kind === SyntaxKind.NamedImports) {
        for (const specifier of bindings.elements) {
          if (specifier.isTypeOnly) continue;
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

  // Phase 4: concatenate statements, dropping imports and pure export wrappers.
  const merged: Statement[] = [];
  /* Every node keeps the module it was parsed from, so a later codegen
   * diagnostic can name the right file and line instead of pointing into the
   * entry module (whose text happens to start at the same offsets). */
  const origins = new Map<object, string>();
  /* A synthesized module (an extension asset loader such as the `gui` `.html`
   * transform) may contain spans copied from another file — a `<script
   * src="app.js">` body, say. Nodes inside such a span keep that file, so a
   * diagnostic points at real source and a plain-`.js` script stays exempt
   * from the type checker. */
  const originOf = (record: ModuleRecord, node: Node): string => {
    const ranges = record.originRanges;
    if (ranges === undefined) return record.path;
    for (const range of ranges) {
      if (node.start >= range.start && node.end <= range.end) return range.file;
    }
    return record.path;
  };
  const trackOrigin = (record: ModuleRecord, root: Node): void => {
    const visit = (node: Node): void => {
      origins.set(node, originOf(record, node));
      const fields = node as unknown as Record<string, unknown>;
      for (const key of Object.keys(fields)) {
        if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value" || key === "raw") {
          continue;
        }
        const value = fields[key];
        if (Array.isArray(value)) {
          for (const child of value) if (isNode(child)) visit(child);
        } else if (isNode(value)) {
          visit(value);
        }
      }
    };
    visit(root);
  };
  const push = (record: ModuleRecord, statement: Statement): void => {
    trackOrigin(record, statement);
    merged.push(statement);
  };
  const first = records[records.length - 1]!;
  for (const record of records) {
    if (deferredModules.has(record.path)) {
      /* A deferred module is evaluated by the wrapper when a script runs. Its
       * external imports stay top-level (codegen resolves them there); eager
       * namespace objects likewise. */
      const topLevel: Statement[] = [];
      const body: Statement[] = [];
      for (const statement of record.sourceFile.statements) {
        if (statement.kind === SyntaxKind.ImportDeclaration) {
          const declaration = statement as ImportDeclaration;
          const resolution = classifyDependency(
            dirname(record.path),
            declaration.moduleSpecifier.value,
            externalSpecifiers,
          );
          if (resolution.kind === "external") topLevel.push(statement);
          const synthetic = namespaceStatements.get(statement);
          if (synthetic) topLevel.push(synthetic);
          continue;
        }
        if (statement.kind === SyntaxKind.ExportDeclaration) {
          const declaration = statement as ExportDeclaration;
          const inner = (declaration as unknown as { declaration?: Statement }).declaration;
          if (inner) body.push(inner);
          continue;
        }
        if (statement.kind === SyntaxKind.ExportAssignment) {
          body.push(exportAssignmentToStatement(record, statement as ExportAssignment));
          continue;
        }
        stripExportModifiers(statement);
        body.push(statement);
      }
      for (const statement of topLevel) push(record, statement);
      for (const statement of deferredModuleFunctions(
        record,
        deferredAliases.get(record.path) ?? [],
        body,
      )) {
        push(record, statement);
      }
      continue;
    }
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
          push(record, statement);
        } else {
          const synthetic = namespaceStatements.get(statement);
          if (synthetic) push(record, synthetic);
        }
        continue;
      }
      if (statement.kind === SyntaxKind.ExportDeclaration) {
        const declaration = statement as ExportDeclaration;
        const inner = (declaration as unknown as { declaration?: Statement }).declaration;
        if (inner) push(record, inner);
        continue;
      }
      if (statement.kind === SyntaxKind.ExportAssignment) {
        const assignment = statement as ExportAssignment;
        push(record, exportAssignmentToStatement(record, assignment));
        continue;
      }
      push(record, statement);
    }
  }

  /* A marker target that stayed eager still needs a wrapper: its exports are
   * already initialised, so the wrapper only builds the namespace object. */
  for (const record of records) {
    if (deferredModules.has(record.path) || !markerTargets.has(record.path)) continue;
    for (const statement of eagerModuleFunctions(record)) push(record, statement);
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
    origins,
    moduleTexts: new Map(records.map((record) => [record.path, record.text])),
    modulePrefixes: records.map((record) => record.prefix),
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


/** Drop `export` / `export default` from a statement moved into a wrapper. */
function stripExportModifiers(statement: Statement): void {
  const modifiers = (statement as { modifiers?: Modifier[] }).modifiers;
  if (!modifiers) return;
  (statement as { modifiers: Modifier[] }).modifiers = modifiers.filter(
    (modifier) => modifier.modifierKind !== ModifierKind.Export && modifier.modifierKind !== ModifierKind.Default,
  );
}

/** True for anything shaped like an AST node (constructed nodes carry no trivia). */
function isNode(value: unknown): value is Node {
  return !!value && typeof value === "object" && "kind" in (value as object);
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
