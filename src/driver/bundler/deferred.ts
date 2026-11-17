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
  type ExportDeclaration,
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
import { AssignmentOperator } from "../../ast/operators.js";
import { TokenKind } from "../../lexer/token.js";
import { isTypeOnlyExport, isTypeOnlyImport } from "./graph.js";
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
  declarationKind: "const" | "let" = "const",
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
    declarationKind,
    start,
    end,
  };
  return { kind: SyntaxKind.VariableStatement, declarationList: list, modifiers: [], start, end };
}

/**
 * `<alias> = <wrapper>()[.<importedName>];`
 *
 * The alias itself is declared at top level (see
 * {@link deferredAliasDeclarations}) so that a function declaration hoisted
 * out of the wrapper can reference it: a module cycle calls that function
 * before the wrapper body has run.
 */
function aliasStatement(alias: DeferredAlias): Statement {
  const callee = identifier(alias.wrapper, alias.start, alias.end);
  const namespace = call(callee, [], alias.start, alias.end);
  const value =
    alias.importedName === undefined || alias.namespace
      ? namespace
      : property(namespace, alias.importedName, alias.start, alias.end);
  return assignmentStatement(alias.name, value, alias.start, alias.end);
}

/** `<name> = <value>;` — used to assign a top-level `let` from inside a wrapper. */
export function assignmentStatement(name: string, value: Expression, start: number, end: number): Statement {
  const assignment = {
    kind: SyntaxKind.BinaryExpression,
    left: identifier(name, start, end),
    operator: AssignmentOperator.Assign,
    right: value,
    operatorToken: { kind: TokenKind.Equals, text: "=", precededByLineBreak: false, start, end },
    start,
    end,
  } as unknown as Expression;
  return {
    kind: SyntaxKind.ExpressionStatement,
    expression: assignment,
    start,
    end,
  };
}

/** `let a, b;` for bindings a deferred module declares at its top level. */
export function letDeclaration(
  names: readonly { readonly name: string; readonly start: number; readonly end: number }[],
): Statement {
  const declarations: VariableDeclaration[] = names.map((entry) => ({
    kind: SyntaxKind.VariableDeclaration,
    name: identifier(entry.name, entry.start, entry.end),
    exclamation: false,
    initializer: undefined,
    start: entry.start,
    end: entry.end,
  }));
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarations,
    declarationKind: "let",
    start: declarations[0]!.start,
    end: declarations[declarations.length - 1]!.end,
  };
  return { kind: SyntaxKind.VariableStatement, declarationList: list, modifiers: [], start: list.start, end: list.end };
}

/** `let <alias>;` for every alias that binds a name, emitted at top level. */
export function deferredAliasDeclarations(aliases: readonly DeferredAlias[]): Statement[] {
  const statements: Statement[] = [];
  for (const alias of aliases) {
    if (alias.sideEffect || alias.name.length === 0) continue;
    statements.push(constDeclaration(alias.name, undefined, alias.start, alias.end, "let"));
  }
  return statements;
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
  moduleOverrides?: Readonly<Record<string, string>>,
): Set<string> {
  const byPath = new Map(records.map((record) => [record.path, record]));
  const dependencyOf = (record: ModuleRecord, specifier: string): ModuleRecord | undefined => {
    const resolution = classifyDependency(dirname(record.path), specifier, externalSpecifiers, moduleOverrides);
    return resolution.kind === "file" ? byPath.get(resolution.path) : undefined;
  };
  /* A type-only import or re-export is erased, so it is not a module edge: it
   * must not make its target eager (`export type { T } from "pkg"` used to
   * pull a whole package into the startup path). */
  const specifierOf = (statement: Statement): string | undefined => {
    if (statement.kind === SyntaxKind.ImportDeclaration) {
      const declaration = statement as ImportDeclaration;
      if (isTypeOnlyImport(declaration)) return undefined;
      return declaration.moduleSpecifier.value;
    }
    if (statement.kind === SyntaxKind.ExportDeclaration) {
      const declaration = statement as ExportDeclaration;
      if (isTypeOnlyExport(declaration)) return undefined;
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
/** The declarations of a `const`/`let`/`var` statement with plain identifiers. */
export function simpleVariableDeclarations(
  statement: Statement,
): { name: string; initializer?: Expression; start: number; end: number }[] | undefined {
  if (statement.kind !== SyntaxKind.VariableStatement) return undefined;
  const list = (statement as VariableStatement).declarationList;
  const declarations: { name: string; initializer?: Expression; start: number; end: number }[] = [];
  for (const declaration of list.declarations) {
    if (declaration.name.kind !== SyntaxKind.Identifier) return undefined;
    declarations.push({
      name: (declaration.name as Identifier).text,
      initializer: declaration.initializer,
      start: declaration.start,
      end: declaration.end,
    });
  }
  return declarations;
}

/**
 * Whether a deferred module's top-level declarations can be split into
 * top-level bindings plus in-wrapper initialisation.
 *
 * Function declarations are hoisted (a cycle may call them before any body
 * runs), so the bindings they reference must be top level too. That works for
 * plain `const`/`let`/`var x = ...`; a destructuring pattern, a class or an
 * enum needs code we do not synthesise, so those modules keep everything inside
 * the wrapper (and behave as before).
 */
export function deferredModuleSplit(record: ModuleRecord): boolean {
  for (const statement of record.sourceFile.statements) {
    if (
      statement.kind === SyntaxKind.ImportDeclaration ||
      statement.kind === SyntaxKind.ExportDeclaration ||
      statement.kind === SyntaxKind.ExportAssignment ||
      statement.kind === SyntaxKind.FunctionDeclaration
    ) {
      continue;
    }
    if (statement.kind === SyntaxKind.VariableStatement) {
      if (simpleVariableDeclarations(statement) === undefined) return false;
      continue;
    }
    if (statement.kind === SyntaxKind.ClassDeclaration || statement.kind === SyntaxKind.EnumDeclaration) {
      return false;
    }
  }
  return true;
}