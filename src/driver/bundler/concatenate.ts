/**
 * Concatenate the merged modules into one source file (bundle phases 4-5).
 *
 * Phase 4 walks every module in dependency order and copies its statements into
 * a single statement list: external imports stay at top level, local imports are
 * dropped (their bindings were rewritten in phase 3), and a deferred module's
 * body is emitted inside its `__xt_deferred_*` wrapper. Phase 5 then renames
 * every top-level declaration and reference to its final unique name.
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
  type SourceFileNode,
  type Statement,
  type VariableDeclaration,
  type VariableDeclarationList,
} from "../../ast/nodes.js";
import { SymbolKind } from "../../binder/binder.js";
import { deferredModuleFunctions, eagerModuleFunctions, type DeferredAlias } from "./deferred.js";
import { classifyDependency } from "./resolve.js";
import { renameSymbol, topLevelSymbols } from "./symbols.js";
import type { ModuleRecord } from "./types.js";

export interface ConcatenateOptions {
  readonly records: readonly ModuleRecord[];
  readonly deferredModules: ReadonlySet<string>;
  readonly deferredAliases: ReadonlyMap<string, readonly DeferredAlias[]>;
  readonly markerTargets: ReadonlySet<string>;
  readonly dynamicTargets: ReadonlySet<string>;
  readonly namespaceStatements: ReadonlyMap<Statement, Statement>;
  readonly externalSpecifiers: ReadonlySet<string>;
  readonly moduleOverrides?: Readonly<Record<string, string>>;
}

export function concatenateModules(options: ConcatenateOptions): SourceFileNode {
  const {
    records,
    deferredModules,
    deferredAliases,
    markerTargets,
    dynamicTargets,
    namespaceStatements,
    externalSpecifiers,
    moduleOverrides,
  } = options;
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
            dirname(record.path), declaration.moduleSpecifier.value, externalSpecifiers, moduleOverrides,
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
          dirname(record.path), declaration.moduleSpecifier.value, externalSpecifiers, moduleOverrides,
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
        const synthetic = namespaceStatements.get(statement);
        if (synthetic) {
          push(record, synthetic);
          continue;
        }
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

  /* A marker or dynamic-import target that stayed eager still needs a wrapper:
   * its exports are already initialised. */
  for (const record of records) {
    if (deferredModules.has(record.path) || (!markerTargets.has(record.path) && !dynamicTargets.has(record.path))) continue;
    for (const statement of eagerModuleFunctions(record)) push(record, statement);
  }

  for (const record of records) {
    for (const symbol of topLevelSymbols(record)) {
      if (symbol.kind === SymbolKind.Import) continue;
      const finalName = record.finalNames.get(symbol.id);
      if (finalName) renameSymbol(symbol, finalName);
    }
  }

  return {
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
  return {
    kind: SyntaxKind.VariableStatement,
    declarationList: list,
    modifiers: [],
    start: assignment.start,
    end: assignment.end,
  };
}
