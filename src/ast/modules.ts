/**
 * Import and export declaration nodes of the xbintsc AST.
 */

import { SyntaxKind } from "./kinds.js";
import type { Expression, Modifier, Node } from "./common.js";
import type { Identifier, StringLiteral } from "./expressions.js";

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

export interface ImportSpecifier extends Node {
  readonly kind: SyntaxKind.ImportSpecifier;
  readonly propertyName?: Identifier;
  readonly name: Identifier;
  readonly isTypeOnly: boolean;
}

export interface NamedImports extends Node {
  readonly kind: SyntaxKind.NamedImports;
  readonly elements: ImportSpecifier[];
}

export interface NamespaceImport extends Node {
  readonly kind: SyntaxKind.NamespaceImport;
  readonly name: Identifier;
}

export interface ImportClause extends Node {
  readonly kind: SyntaxKind.ImportClause;
  readonly name?: Identifier;
  readonly namedBindings?: NamedImports | NamespaceImport;
  readonly isTypeOnly: boolean;
}

export interface ImportDeclaration extends Node {
  readonly kind: SyntaxKind.ImportDeclaration;
  readonly importClause?: ImportClause;
  readonly moduleSpecifier: StringLiteral;
  readonly attributes: { name: string; value: string }[];
}

export interface ExportSpecifier extends Node {
  readonly kind: SyntaxKind.ExportSpecifier;
  readonly propertyName?: Identifier;
  readonly name: Identifier;
  readonly isTypeOnly: boolean;
}

export interface NamedExports extends Node {
  readonly kind: SyntaxKind.NamedExports;
  readonly elements: ExportSpecifier[];
}

export interface ExportDeclaration extends Node {
  readonly kind: SyntaxKind.ExportDeclaration;
  readonly modifiers: Modifier[];
  readonly exportClause?: NamedExports | NamespaceExport;
  readonly moduleSpecifier?: StringLiteral;
  readonly isTypeOnly: boolean;
}

export interface NamespaceExport extends Node {
  readonly kind: SyntaxKind.NamespaceImport;
  readonly name: Identifier;
}

export interface ExportAssignment extends Node {
  readonly kind: SyntaxKind.ExportAssignment;
  readonly isExportEquals: boolean;
  readonly expression: Expression;
}
