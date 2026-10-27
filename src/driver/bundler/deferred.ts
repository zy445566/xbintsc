/**
 * Deferred module evaluation for `<script src>` imports.
 *
 * A page's external script is AOT-compiled and registered at module
 * initialisation, but its imports are ordinary ES imports: the bundler merges
 * them ahead of the page's entry and they run before `loadHTML`. A browser
 * evaluates a module graph after the document exists, so a module that touches
 * the DOM from its top level sees an empty document here.
 *
 * The `.html` loader marks each relative import of a `<script src>` with a
 * synthetic `__xt_deferred_module` binding. The bundler turns the target module
 * (and its local dependency graph) into a function that returns its exports,
 * memoised by the runtime's `__xt_defer`; the generated script function calls
 * that wrapper after `loadHTML`. Modules that are also imported eagerly stay
 * eager and get a thin wrapper over their already-initialised exports.
 */

import { dirname } from "node:path";
import {
  NodeFlags,
  SyntaxKind,
  type Block,
  type CallExpression,
  type Expression,
  type FunctionDeclaration,
  type Identifier,
  type ImportDeclaration,
  type ObjectLiteralElementLike,
  type PropertyAssignment,
  type ReturnStatement,
  type Statement,
  type StringLiteral,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../../ast/nodes.js";
import { SymbolKind } from "../../binder/types.js";
import { classifyDependency } from "./resolve.js";
import { topLevelSymbols } from "./symbols.js";
import type { ModuleRecord } from "./types.js";

/** The imported name the `.html` loader uses to mark a deferred import. */
export const DEFERRED_MARKER = "__xt_deferred_module";

/** Whether `statement` is a synthetic deferred-import marker. */
export function isDeferredMarkerImport(statement: Statement): boolean {
  if (statement.kind !== SyntaxKind.ImportDeclaration) return false;
  const declaration = statement as ImportDeclaration;
  const bindings = declaration.importClause?.namedBindings;
  if (!bindings || bindings.kind !== SyntaxKind.NamedImports) return false;
  return bindings.elements.some(
    (element) => (element.propertyName?.text ?? element.name.text) === DEFERRED_MARKER,
  );
}

/** The final name of a module's wrapper function (`() => exports`). */
export function deferredWrapperName(record: ModuleRecord): string {
  return `__xt_deferred_${record.prefix.replace(/_$/, "")}`;
}

function deferredImplName(record: ModuleRecord): string {
  return `${deferredWrapperName(record)}_impl`;
}

/** A unique local name for an import inside a deferred module's wrapper. */
export function deferredAliasName(record: ModuleRecord, index: number, hint: string): string {
  const clean = hint.replace(/[^A-Za-z0-9_$]/g, "_");
  return `${record.prefix}imp_${index}_${clean}`;
}

function identifier(text: string, start: number, end: number): Identifier {
  return { kind: SyntaxKind.Identifier, text, start, end };
}

function stringLiteral(value: string, start: number, end: number): StringLiteral {
  const raw = JSON.stringify(value);
  return { kind: SyntaxKind.StringLiteral, text: raw, value, raw, start, end };
}

function call(callee: Expression, args: Expression[], start: number, end: number): CallExpression {
  return {
    kind: SyntaxKind.CallExpression,
    expression: callee,
    typeArguments: [],
    arguments: args,
    optional: false,
    start,
    end,
  };
}

function property(object: Expression, name: string, start: number, end: number): Expression {
  return {
    kind: SyntaxKind.PropertyAccessExpression,
    expression: object,
    name: identifier(name, start, end),
    optional: false,
    start,
    end,
  };
}

function constDeclaration(
  name: string,
  initializer: Expression | undefined,
  start: number,
  end: number,
): VariableStatement {
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name: identifier(name, start, end),
    exclamation: false,
    initializer,
    start,
    end,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarations: [declaration],
    declarationKind: "const",
    start,
    end,
  };
  return { kind: SyntaxKind.VariableStatement, declarationList: list, modifiers: [], start, end };
}

/** `const <alias> = <wrapper>()[.<importedName>];` */
function aliasStatement(alias: DeferredAlias): VariableStatement {
  const callee = identifier(alias.wrapper, alias.start, alias.end);
  const namespace = call(callee, [], alias.start, alias.end);
  const value =
    alias.importedName === undefined || alias.namespace
      ? namespace
      : property(namespace, alias.importedName, alias.start, alias.end);
  return constDeclaration(alias.name, value, alias.start, alias.end);
}

/** A wrapper call used for a side-effect-only import. */
function sideEffectStatement(alias: DeferredAlias): Statement {
  return {
    kind: SyntaxKind.ExpressionStatement,
    expression: call(identifier(alias.wrapper, alias.start, alias.end), [], alias.start, alias.end),
    start: alias.start,
    end: alias.end,
  };
}

function isIdentifierText(text: string): boolean {
  return /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(text);
}

function returnObject(entries: readonly DeferredExport[], start: number, end: number): ReturnStatement {
  const properties: ObjectLiteralElementLike[] = entries.map((entry) => {
    const name = isIdentifierText(entry.name)
      ? identifier(entry.name, start, end)
      : stringLiteral(entry.name, start, end);
    const assignment: PropertyAssignment = {
      kind: SyntaxKind.PropertyAssignment,
      name,
      initializer: identifier(entry.local, start, end),
      start,
      end,
    };
    return assignment;
  });
  return {
    kind: SyntaxKind.ReturnStatement,
    expression: { kind: SyntaxKind.ObjectLiteralExpression, properties, start, end },
    start,
    end,
  };
}

function functionDeclaration(
  name: string,
  statements: Statement[],
  start: number,
  end: number,
): FunctionDeclaration {
  const body: Block = { kind: SyntaxKind.Block, statements, start, end };
  return {
    kind: SyntaxKind.FunctionDeclaration,
    name: identifier(name, start, end),
    modifiers: [],
    typeParameters: [],
    parameters: [],
    body,
    flags: NodeFlags.None,
    start,
    end,
  };
}

export interface DeferredAlias {
  readonly name: string;
  readonly wrapper: string;
  readonly importedName?: string;
  /** The import's local binding, used to fix up `export { name }` re-exports. */
  readonly local?: string;
  readonly namespace?: boolean;
  readonly sideEffect?: boolean;
  readonly start: number;
  readonly end: number;
}

export interface DeferredExport {
  readonly name: string;
  readonly local: string;
}

/** Every runtime export of `record` (type-only declarations are skipped). */
export function valueExports(record: ModuleRecord): DeferredExport[] {
  const typeNames = new Set<string>();
  for (const symbol of topLevelSymbols(record)) {
    if (symbol.kind !== SymbolKind.Interface && symbol.kind !== SymbolKind.TypeAlias) continue;
    const finalName = record.finalNames.get(symbol.id);
    if (finalName !== undefined) typeNames.add(finalName);
  }
  const out: DeferredExport[] = [];
  for (const [name, local] of record.exports) {
    if (typeNames.has(local)) continue;
    out.push({ name, local });
  }
  return out;
}

/**
 * Build the body of a deferred module:
 *
 *     function wrapper() { return __xt_defer(id, impl); }
 *     function impl() { <aliases> <body> return { exports }; }
 *
 * `body` must already have imports and export wrappers removed.
 */
export function deferredModuleFunctions(
  record: ModuleRecord,
  aliases: readonly DeferredAlias[],
  body: readonly Statement[],
): Statement[] {
  const start = record.sourceFile.start;
  const end = record.sourceFile.end;
  const statements: Statement[] = [];
  for (const alias of aliases) {
    statements.push(alias.sideEffect ? sideEffectStatement(alias) : aliasStatement(alias));
  }
  for (const statement of body) statements.push(statement);
  statements.push(returnObject(valueExports(record), start, end));

  const impl = functionDeclaration(deferredImplName(record), statements, start, end);
  const wrapperBody: Statement[] = [
    {
      kind: SyntaxKind.ReturnStatement,
      expression: call(
        identifier("__xt_defer", start, end),
        [
          stringLiteral(record.prefix, start, end),
          identifier(deferredImplName(record), start, end),
        ],
        start,
        end,
      ),
      start,
      end,
    },
  ];
  const wrapper = functionDeclaration(deferredWrapperName(record), wrapperBody, start, end);
  return [wrapper, impl];
}

/**
 * A wrapper for a module that stayed eager: it only exposes the exports whose
 * values other modules already initialised, so the marker call shape
 * (`imports()` → namespace object) still works.
 */
export function eagerModuleFunctions(record: ModuleRecord): Statement[] {
  const start = record.sourceFile.start;
  const end = record.sourceFile.end;
  return [
    functionDeclaration(
      deferredWrapperName(record),
      [returnObject(valueExports(record), start, end)],
      start,
      end,
    ),
  ];
}

/**
 * Which modules must be evaluated lazily: everything reachable from a marker
 * import, except modules the entry also reaches through a normal import.
 */
export function computeDeferredModules(
  records: readonly ModuleRecord[],
  externalSpecifiers: ReadonlySet<string>,
): Set<string> {
  const byPath = new Map(records.map((record) => [record.path, record]));
  const dependencyOf = (record: ModuleRecord, specifier: string): ModuleRecord | undefined => {
    const resolution = classifyDependency(dirname(record.path), specifier, externalSpecifiers);
    return resolution.kind === "file" ? byPath.get(resolution.path) : undefined;
  };
  const specifierOf = (statement: Statement): string | undefined => {
    if (statement.kind === SyntaxKind.ImportDeclaration) {
      const declaration = statement as ImportDeclaration;
      if (declaration.importClause?.isTypeOnly) return undefined;
      return declaration.moduleSpecifier.value;
    }
    if (statement.kind === SyntaxKind.ExportDeclaration) {
      const declaration = statement as { moduleSpecifier?: { value: string } };
      return declaration.moduleSpecifier?.value;
    }
    return undefined;
  };

  /* Modules reachable from the entry without crossing a marker are evaluated
   * eagerly; a module in this set must not be wrapped, or an eager importer
   * would reference a variable its wrapper owns. */
  const eager = new Set<string>();
  const entry = records[records.length - 1];
  const visitEager = (record: ModuleRecord | undefined): void => {
    if (!record || eager.has(record.path)) return;
    eager.add(record.path);
    for (const statement of record.sourceFile.statements) {
      if (isDeferredMarkerImport(statement)) continue;
      const specifier = specifierOf(statement);
      if (specifier !== undefined) visitEager(dependencyOf(record, specifier));
    }
  };
  visitEager(entry);

  const deferred = new Set<string>();
  const queue: ModuleRecord[] = [];
  for (const record of records) {
    for (const statement of record.sourceFile.statements) {
      if (!isDeferredMarkerImport(statement)) continue;
      const target = dependencyOf(record, (statement as ImportDeclaration).moduleSpecifier.value);
      if (target && !eager.has(target.path)) queue.push(target);
    }
  }
  while (queue.length > 0) {
    const record = queue.pop()!;
    if (eager.has(record.path) || deferred.has(record.path)) continue;
    deferred.add(record.path);
    for (const statement of record.sourceFile.statements) {
      const specifier = specifierOf(statement);
      if (specifier === undefined) continue;
      const target = dependencyOf(record, specifier);
      if (target && !eager.has(target.path)) queue.push(target);
    }
  }
  return deferred;
}
