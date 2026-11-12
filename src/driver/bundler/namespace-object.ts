/**
 * Build the synthetic object literal that stands in for a namespace binding.
 *
 * `import * as ns from "./mod"` and `export * as ns from "./mod"` are lowered
 * to `const <final> = { exportName: finalName, ... }`, because the merged
 * module has no runtime namespace object to point at.
 */

import {
  SyntaxKind,
  type ObjectLiteralExpression,
  type PropertyAssignment,
  type Statement,
  type VariableDeclaration,
  type VariableDeclarationList,
} from "../../ast/nodes.js";
import type { ModuleRecord } from "./types.js";

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
