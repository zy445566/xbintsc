/**
 * Build the synthetic object literal that stands in for a namespace binding.
 *
 * `import * as ns from "./mod"` and `export * as ns from "./mod"` are lowered
 * to `const <final> = { exportName: finalName, ... }`, because the merged
 * module has no runtime namespace object to point at.
 */

import {
  SyntaxKind,
  type CallExpression,
  type Identifier,
  type ObjectLiteralExpression,
  type PropertyAssignment,
  type Statement,
  type VariableDeclaration,
  type VariableDeclarationList,
} from "../../ast/nodes.js";
import type { ModuleRecord } from "./types.js";

/**
 * Lower a *deferred* module's `export * as ns from "./dep"` to
 * `const <final> = __xt_deferred_<dep>()`.
 *
 * The dependency's export names only exist inside its own wrapper function, so
 * an object literal of final names would reference names that are not in scope
 * (TS3002) or be `undefined` at run time. The wrapper returns the dependency's
 * exports object, which is what the namespace export needs, and calling it also
 * initialises the dependency first.
 */
export function deferredNamespaceToStatement(
  statement: Statement,
  finalName: string,
  wrapperName: string,
): Statement {
  const callee: Identifier = {
    kind: SyntaxKind.Identifier,
    text: wrapperName,
    start: statement.start,
    end: statement.start,
  };
  const initializer: CallExpression = {
    kind: SyntaxKind.CallExpression,
    expression: callee,
    arguments: [],
    typeArguments: [],
    optional: false,
    start: statement.start,
    end: statement.end,
  };
  return variableStatement(statement, finalName, initializer);
}

/** Lower `import * as ns from "./mod"` to `const m0_ns = { ...exports }`. */
export function namespaceImportToStatement(
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
  return variableStatement(statement, finalName, initializer);
}

/** `const <finalName> = <initializer>` at the statement's offsets. */
function variableStatement(
  statement: Statement,
  finalName: string,
  initializer: ObjectLiteralExpression | CallExpression,
): Statement {
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
