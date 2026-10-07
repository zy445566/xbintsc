/**
 * CommonJS compatibility for `node_modules`.
 *
 * xbintsc is ESM-first: relative `.ts` files and ESM packages are bundled into
 * one module and `require()` in user code is rejected. npm, however, still
 * ships a lot of CommonJS. This pass lets a CommonJS file *inside
 * `node_modules`* keep working by lowering its free `module` / `exports` /
 * `require` references to per-module synthetic bindings:
 *
 *   const <prefix>$cjs_module  = { exports: {} };
 *   const <prefix>$cjs_exports = <prefix>$cjs_module.exports;
 *
 * `module.exports = x` and `exports.x = y` then behave as they do in Node, and
 * `require("x")` is rewritten to the already-bundled dependency's exports
 * object. User code (anything outside `node_modules`) is left untouched, so
 * the code generator still reports its `require` use.
 */

import { dirname } from "node:path";
import {
  SyntaxKind,
  type BindingElement,
  type CallExpression,
  type Expression,
  type Identifier,
  type ImportDeclaration,
  type ImportSpecifier,
  type NamedImports,
  type NamespaceImport,
  type Node,
  type ObjectBindingPattern,
  type ObjectLiteralExpression,
  type ObjectLiteralElementLike,
  type PropertyAccessExpression,
  type SourceFileNode,
  type Statement,
  type StringLiteral,
  type VariableDeclaration,
  type VariableDeclarationList,
  type VariableStatement,
} from "../../ast/nodes.js";
import type { BindResult } from "../../binder/binder.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { moduleScope, renameReferences } from "./symbols.js";
import type { DependencyResolution, ModuleRecord } from "./types.js";

/** The specifier of every `require("...")` call in a module. */
export interface RequireSite {
  /** The call node, for the diagnostic location. */
  readonly node: CallExpression;
  /** The string literal argument's value. */
  readonly specifier: string;
}

/** True when a path lives under a `node_modules` directory. */
export function isNodeModulesPath(path: string): boolean {
  return path.split(/[\\/]+/).includes("node_modules");
}

/** True when a module references `require`, `module` or `exports` freely. */
export function usesCommonJS(bind: BindResult): boolean {
  return bind.unresolved.some(
    (identifier) =>
      identifier.text === "require" || identifier.text === "module" || identifier.text === "exports",
  );
}

/**
 * A `require` identifier is CommonJS only when it has no binding; a local
 * `function require() {}` or parameter must not be treated as the CJS hook.
 */
function isFreeRequire(identifier: Identifier, bind: BindResult): boolean {
  return identifier.text === "require" && !bind.symbolOfIdentifier.get(identifier);
}

/** Every `require("<literal>")` call in a module. */
export function collectRequireSites(sourceFile: SourceFileNode, bind: BindResult): RequireSite[] {
  const sites: RequireSite[] = [];
  forEachNode(sourceFile, (node) => {
    if (node.kind !== SyntaxKind.CallExpression) return;
    const call = node as CallExpression;
    const callee = call.expression;
    if (callee.kind !== SyntaxKind.Identifier) return;
    if (!isFreeRequire(callee as Identifier, bind)) return;
    const first = call.arguments[0];
    if (first && first.kind === SyntaxKind.StringLiteral) {
      sites.push({ node: call, specifier: (first as StringLiteral).value });
    }
  });
  return sites;
}

export interface CommonJSLoweringContext {
  readonly resolve: (fromDir: string, specifier: string) => DependencyResolution;
  readonly recordFor: (path: string) => ModuleRecord | undefined;
  readonly diagnostics: DiagnosticBag;
}

/**
 * Phase A: give a CommonJS module its `module` / `exports` state, rewrite the
 * free references, and record its export surface (whole-object `default` plus
 * every syntactically detectable named export). Runs before the ESM export
 * collection so re-exports of a CommonJS module resolve.
 */
export function prepareCommonJS(record: ModuleRecord): void {
  if (!record.commonjs) return;
  const statements = record.sourceFile.statements as Statement[];
  const anchor = statements[0]?.start ?? record.sourceFile.start;

  const moduleName = `${record.prefix}$cjs_module`;
  const exportsName = `${record.prefix}$cjs_exports`;
  record.cjsModule = moduleName;

  const hasExports = record.bind.unresolved.some((identifier) => identifier.text === "exports");
  const hasModule = record.bind.unresolved.some((identifier) => identifier.text === "module");
  // Detect exports before rewriting `module`/`exports`, since the detection
  // matches on their original names.
  const exportNames = detectExportNames(record.sourceFile, record.bind);
  for (const identifier of record.bind.unresolved) {
    if (identifier.text === "module" && hasModule) (identifier as { text: string }).text = moduleName;
    else if (identifier.text === "exports" && hasExports) (identifier as { text: string }).text = exportsName;
  }

  const prefixStatements: Statement[] = [
    variableStatement(moduleName, objectLiteral([propertyAssignment("exports", objectLiteral([]))]), anchor),
  ];
  prefixStatements.push(variableStatement(exportsName, member(moduleName, "exports", anchor), anchor));

  const suffixStatements: Statement[] = [];
  const defaultName = `${record.prefix}$cjs_default`;
  suffixStatements.push(variableStatement(defaultName, member(moduleName, "exports", anchor), anchor));
  record.exports.set("default", defaultName);
  for (const name of exportNames) {
    const local = `${record.prefix}$cjs_${name}`;
    suffixStatements.push(variableStatement(local, member(member(moduleName, "exports", anchor), name, anchor), anchor));
    record.exports.set(name, local);
  }

  statements.unshift(...prefixStatements);
  statements.push(...suffixStatements);
}

/**
 * Phase B: rewrite every `require("x")` call to the bundled dependency's
 * exports. Runs after the ESM export collection because a required ESM module
 * is exposed as a synthetic namespace object.
 */
export function lowerCommonJSRequires(record: ModuleRecord, context: CommonJSLoweringContext): void {
  if (!record.commonjs) return;
  const statements = record.sourceFile.statements as Statement[];
  const anchor = statements[0]?.start ?? record.sourceFile.start;
  const imports: Statement[] = [];
  const namespaces: Statement[] = [];
  let counter = 0;

  // Before rewriting requires, turn `var x = require("node:fs")` into an ESM
  // namespace import. Extension/built-in modules are not first-class values in
  // the code generator, so binding one to a local name has to make the local
  // name an import binding rather than an assignment of the module object.
  hoistExternalNamespaceRequires(record, context, statements, anchor);

  const replace = (node: Node): Node | undefined => {
    if (node.kind !== SyntaxKind.CallExpression) return undefined;
    const call = node as CallExpression;
    const callee = call.expression;
    if (callee.kind !== SyntaxKind.Identifier) return undefined;
    if (!isFreeRequire(callee as Identifier, record.bind)) return undefined;
    const first = call.arguments[0];
    if (!first || first.kind !== SyntaxKind.StringLiteral) {
      context.diagnostics.error(
        DiagnosticCode.UnsupportedFeature,
        "CommonJS `require()` with a non-literal argument is not supported",
        node,
        record.path,
      );
      return undefinedLiteral(anchor);
    }
    const specifier = (first as StringLiteral).value;
    const resolution = context.resolve(dirname(record.path), specifier);
    if (resolution.kind === "missing") {
      context.diagnostics.error(
        DiagnosticCode.CodegenError,
        `Cannot resolve module '${specifier}' required from '${record.path}'`,
        node,
        record.path,
      );
      return undefinedLiteral(anchor);
    }
    if (resolution.kind === "external") {
      const name = `${record.prefix}$ext_${counter++}`;
      imports.push(defaultImport(name, specifier, anchor));
      return identifier(name, anchor);
    }
    const dependency = context.recordFor(resolution.path);
    if (!dependency) {
      context.diagnostics.error(
        DiagnosticCode.CodegenError,
        `Cannot resolve module '${specifier}' required from '${record.path}'`,
        node,
        record.path,
      );
      return undefinedLiteral(anchor);
    }
    if (dependency.commonjs && dependency.cjsModule) {
      return member(dependency.cjsModule, "exports", anchor);
    }
    // An ESM dependency is exposed to `require` as a namespace object.
    const name = `${record.prefix}$req_${counter++}`;
    namespaces.push(variableStatement(name, namespaceObject(dependency, anchor), anchor));
    return identifier(name, anchor);
  };

  for (const statement of statements) replaceInTree(statement, replace);
  if (imports.length > 0 || namespaces.length > 0) statements.unshift(...imports, ...namespaces);
}

/**
 * Statically detect a CommonJS module's named exports so ESM consumers can
 * import them: `exports.x = ...`, `module.exports.x = ...`, an object-literal
 * `module.exports = { ... }`, and the `Object.defineProperty`/`Object.assign`
 * forms.
 */
function detectExportNames(sourceFile: SourceFileNode, bind: BindResult): string[] {
  const names = new Set<string>();
  forEachNode(sourceFile, (node) => {
    if (node.kind === SyntaxKind.BinaryExpression) {
      const assignment = node as unknown as { left: Expression; operator: string; right: Expression };
      if (assignment.operator !== "=") return;
      const name = namedExportTarget(assignment.left, bind);
      if (name) names.add(name);
      if (isModuleExports(assignment.left, bind) && assignment.right.kind === SyntaxKind.ObjectLiteralExpression) {
        for (const property of (assignment.right as ObjectLiteralExpression).properties) {
          const key = literalPropertyName(property);
          if (key) names.add(key);
        }
      }
    } else if (node.kind === SyntaxKind.CallExpression) {
      const call = node as CallExpression;
      const callee = call.expression;
      if (callee.kind !== SyntaxKind.PropertyAccessExpression) return;
      const access = callee as PropertyAccessExpression;
      if (access.expression.kind !== SyntaxKind.Identifier) return;
      const method = access.name.text;
      const target = call.arguments[0];
      const value = call.arguments[1];
      if (access.expression.kind === SyntaxKind.Identifier && access.expression.text === "Object") {
        if (method === "defineProperty" && target && value && value.kind === SyntaxKind.StringLiteral) {
          if (isModuleExportsOrExports(target, bind)) names.add((value as StringLiteral).value);
        } else if (method === "assign" && target && isModuleExportsOrExports(target, bind) && value) {
          if (value.kind === SyntaxKind.ObjectLiteralExpression) {
            for (const property of (value as ObjectLiteralExpression).properties) {
              const key = literalPropertyName(property);
              if (key) names.add(key);
            }
          }
        }
      }
    }
  });
  // Keep only names that are safe to snapshot via `.name` access.
  return [...names].filter((name) => /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(name) && name !== "default");
}

/** The property name assigned through `exports.x` / `module.exports.x`. */
function namedExportTarget(left: Expression, bind: BindResult): string | undefined {
  if (left.kind !== SyntaxKind.PropertyAccessExpression) return undefined;
  const access = left as PropertyAccessExpression;
  const owner = access.expression;
  if (isModuleExportsOrExports(owner, bind)) return access.name.text;
  return undefined;
}

function isModuleExportsOrExports(expression: Expression, bind: BindResult): boolean {
  if (expression.kind !== SyntaxKind.Identifier) {
    if (expression.kind !== SyntaxKind.PropertyAccessExpression) return false;
    return isModuleExports(expression, bind);
  }
  const identifier = expression as Identifier;
  return identifier.text === "exports" && !bind.symbolOfIdentifier.get(identifier);
}

function isModuleExports(expression: Expression, bind: BindResult): boolean {
  if (expression.kind !== SyntaxKind.PropertyAccessExpression) return false;
  const access = expression as PropertyAccessExpression;
  if (access.name.text !== "exports") return false;
  const owner = access.expression;
  if (owner.kind !== SyntaxKind.Identifier) return false;
  const identifier = owner as Identifier;
  return identifier.text === "module" && !bind.symbolOfIdentifier.get(identifier);
}

function literalPropertyName(property: ObjectLiteralElementLike): string | undefined {
  if (property.kind === SyntaxKind.ShorthandPropertyAssignment) {
    return (property as unknown as { name: Identifier }).name.text;
  }
  if (property.kind === SyntaxKind.PropertyAssignment) {
    const name = (property as unknown as { name: Node }).name;
    if (name.kind === SyntaxKind.Identifier || name.kind === SyntaxKind.StringLiteral) {
      return (name as unknown as { text?: string; value?: string }).text ?? (name as StringLiteral).value;
    }
  }
  return undefined;
}

/** Build `{ default: <dep default>, ...named }` for a required ESM module. */
function namespaceObject(dependency: ModuleRecord, anchor: number): ObjectLiteralExpression {
  const properties: ObjectLiteralElementLike[] = [];
  const names = [...dependency.exports.keys()].filter((name) => /^[A-Za-z_$][A-Za-z0-9_$]*$/.test(name));
  for (const name of ["default", ...names.filter((name) => name !== "default")]) {
    const target = dependency.exports.get(name);
    if (target) properties.push(propertyAssignment(name, identifier(target, anchor)));
  }
  return objectLiteral(properties);
}

// ---------------------------------------------------------------------------
// Generic, allocation-light AST helpers (constructed nodes carry no trivia).
// ---------------------------------------------------------------------------

function isNode(value: unknown): value is Node {
  return !!value && typeof value === "object" && "kind" in (value as object);
}

/** Pre-order walk over every child node. */
function forEachNode(root: Node, visit: (node: Node) => void): void {
  visit(root);
  const record = root as unknown as Record<string, unknown>;
  for (const key of Object.keys(record)) {
    if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value" || key === "raw") continue;
    const value = record[key];
    if (Array.isArray(value)) {
      for (const child of value) if (isNode(child)) forEachNode(child, visit);
    } else if (isNode(value)) {
      forEachNode(value, visit);
    }
  }
}

/** Children-first walk that lets `replace` swap a node for a new expression. */
function replaceInTree(node: Node, replace: (node: Node) => Node | undefined): Node {
  const record = node as unknown as Record<string, unknown>;
  for (const key of Object.keys(record)) {
    if (key === "kind" || key === "start" || key === "end" || key === "text" || key === "value" || key === "raw") continue;
    const value = record[key];
    if (Array.isArray(value)) {
      for (let index = 0; index < value.length; index++) {
        if (isNode(value[index])) value[index] = replaceInTree(value[index] as Node, replace);
      }
    } else if (isNode(value)) {
      record[key] = replaceInTree(value, replace);
    }
  }
  return replace(node) ?? node;
}

function identifier(text: string, anchor: number): Identifier {
  return { kind: SyntaxKind.Identifier, text, start: anchor, end: anchor };
}

function member(object: string | Expression, name: string, anchor: number): PropertyAccessExpression {
  const expression: Expression = typeof object === "string" ? identifier(object, anchor) : object;
  return {
    kind: SyntaxKind.PropertyAccessExpression,
    expression,
    name: identifier(name, anchor),
    optional: false,
    start: anchor,
    end: anchor,
  };
}

function objectLiteral(properties: ObjectLiteralElementLike[]): ObjectLiteralExpression {
  return { kind: SyntaxKind.ObjectLiteralExpression, properties, start: 0, end: 0 };
}

function propertyAssignment(name: string, initializer: Expression): ObjectLiteralElementLike {
  return {
    kind: SyntaxKind.PropertyAssignment,
    name: identifier(name, 0),
    initializer,
    start: 0,
    end: 0,
  };
}

function undefinedLiteral(anchor: number): Expression {
  return { kind: SyntaxKind.UndefinedKeyword, start: anchor, end: anchor };
}

function variableStatement(name: string, initializer: Expression, anchor: number): VariableStatement {
  const declaration: VariableDeclaration = {
    kind: SyntaxKind.VariableDeclaration,
    name: identifier(name, anchor),
    exclamation: false,
    initializer,
    start: anchor,
    end: anchor,
  };
  const list: VariableDeclarationList = {
    kind: SyntaxKind.VariableDeclarationList,
    declarationKind: "const",
    declarations: [declaration],
    start: anchor,
    end: anchor,
  };
  return { kind: SyntaxKind.VariableStatement, declarationList: list, modifiers: [], start: anchor, end: anchor };
}

function defaultImport(name: string, specifier: string, anchor: number): ImportDeclaration {
  const moduleSpecifier: StringLiteral = {
    kind: SyntaxKind.StringLiteral,
    text: JSON.stringify(specifier),
    value: specifier,
    raw: JSON.stringify(specifier),
    start: anchor,
    end: anchor,
  };
  return {
    kind: SyntaxKind.ImportDeclaration,
    importClause: { kind: SyntaxKind.ImportClause, name: identifier(name, anchor), isTypeOnly: false, start: anchor, end: anchor },
    moduleSpecifier,
    attributes: [],
    start: anchor,
    end: anchor,
  };
}

/**
 * Rewrite `var x = require("<external>")` into an ESM import so that the
 * extension / built-in module can be used as a first-class value:
 *
 *   const os  = require("node:os")          -> import * as os from "node:os"
 *   const { join } = require("node:path")   -> import { join } from "node:path"
 *
 * Only simple identifier / object-literal bindings at the top level are
 * handled; a require nested inside an expression falls back to the default
 * import path, which supports direct member access but not value use.
 */
function hoistExternalNamespaceRequires(
  record: ModuleRecord,
  context: CommonJSLoweringContext,
  statements: Statement[],
  anchor: number,
): void {
  const scope = moduleScope(record);
  if (!scope) return;
  for (let index = 0; index < statements.length; index++) {
    const statement = statements[index];
    if (!statement || statement.kind !== SyntaxKind.VariableStatement) continue;
    const list = (statement as VariableStatement).declarationList;
    if (list.declarations.length !== 1) continue;
    const declaration = list.declarations[0];
    if (!declaration) continue;
    const initializer = declaration.initializer;
    if (!initializer || initializer.kind !== SyntaxKind.CallExpression) continue;
    const call = initializer as CallExpression;
    const callee = call.expression;
    if (callee.kind !== SyntaxKind.Identifier || !isFreeRequire(callee as Identifier, record.bind)) continue;
    const first = call.arguments[0];
    if (!first || first.kind !== SyntaxKind.StringLiteral) continue;
    const specifier = (first as StringLiteral).value;
    if (context.resolve(dirname(record.path), specifier).kind !== "external") continue;

    if (declaration.name.kind === SyntaxKind.Identifier) {
      const symbol = scope.symbols.get(declaration.name.text);
      const finalName = symbol ? record.finalNames.get(symbol.id) : undefined;
      if (!symbol || !finalName) continue;
      renameReferences(symbol, finalName);
      statements[index] = namespaceImport(finalName, specifier, anchor);
      continue;
    }

    if (declaration.name.kind === SyntaxKind.ObjectBindingPattern) {
      const hoisted = hoistObjectBinding(record, scope, declaration.name as ObjectBindingPattern, specifier, anchor);
      if (hoisted) statements[index] = hoisted;
    }
  }
}

/** Build `import { a, b as c } from "spec"` for a `const { ... } = require` binding. */
function hoistObjectBinding(
  record: ModuleRecord,
  scope: NonNullable<ReturnType<typeof moduleScope>>,
  pattern: ObjectBindingPattern,
  specifier: string,
  anchor: number,
): ImportDeclaration | undefined {
  const specifiers: ImportSpecifier[] = [];
  for (const element of pattern.elements as BindingElement[]) {
    if (element.dotDotDotToken || element.initializer || element.name.kind !== SyntaxKind.Identifier) return undefined;
    const localText = (element.name as Identifier).text;
    const importedName = element.propertyName
      ? element.propertyName.kind === SyntaxKind.Identifier
        ? (element.propertyName as Identifier).text
        : (element.propertyName as StringLiteral).value
      : localText;
    const symbol = scope.symbols.get(localText);
    const finalName = symbol ? record.finalNames.get(symbol.id) : undefined;
    if (!symbol || !finalName) return undefined;
    renameReferences(symbol, finalName);
    specifiers.push({
      kind: SyntaxKind.ImportSpecifier,
      propertyName: importedName === finalName ? undefined : identifier(importedName, anchor),
      name: identifier(finalName, anchor),
      isTypeOnly: false,
      start: anchor,
      end: anchor,
    });
  }
  if (specifiers.length === 0) return undefined;
  const namedBindings: NamedImports = { kind: SyntaxKind.NamedImports, elements: specifiers, start: anchor, end: anchor };
  return importDeclaration(namedBindings, specifier, anchor);
}

function namespaceImport(name: string, specifier: string, anchor: number): ImportDeclaration {
  const namedBindings = {
    kind: SyntaxKind.NamespaceImport as const,
    name: identifier(name, anchor),
    start: anchor,
    end: anchor,
  };
  return importDeclaration(namedBindings, specifier, anchor);
}

function importDeclaration(
  namedBindings: NamedImports | NamespaceImport,
  specifier: string,
  anchor: number,
): ImportDeclaration {
  const moduleSpecifier: StringLiteral = {
    kind: SyntaxKind.StringLiteral,
    text: JSON.stringify(specifier),
    value: specifier,
    raw: JSON.stringify(specifier),
    start: anchor,
    end: anchor,
  };
  return {
    kind: SyntaxKind.ImportDeclaration,
    importClause: {
      kind: SyntaxKind.ImportClause,
      namedBindings,
      isTypeOnly: false,
      start: anchor,
      end: anchor,
    },
    moduleSpecifier,
    attributes: [],
    start: anchor,
    end: anchor,
  };
}
