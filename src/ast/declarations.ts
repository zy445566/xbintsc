/**
 * Declaration nodes of the xbintsc AST (functions, classes, interfaces, ...).
 */

import { NodeFlags, SyntaxKind } from "./kinds.js";
import type { Expression, Modifier, Node, TypeNode } from "./common.js";
import type { Identifier, PropertyName, StringLiteral } from "./expressions.js";
import type { Block } from "./statements.js";
import type { TypeElement } from "./types.js";

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

export interface Parameter extends Node {
  readonly kind: SyntaxKind.Parameter;
  readonly name: BindingName;
  readonly modifiers: Modifier[];
  readonly dotDotDotToken: boolean;
  readonly questionToken: boolean;
  readonly type?: TypeNode;
  readonly initializer?: Expression;
}

// -- binding patterns (destructuring) --------------------------------------

export type BindingName = Identifier | ArrayBindingPattern | ObjectBindingPattern;

export interface BindingElement extends Node {
  readonly kind: SyntaxKind.BindingElement;
  readonly name: BindingName;
  /** For `{ a: b }` the `a`; absent for shorthand and array patterns. */
  readonly propertyName?: PropertyName;
  readonly dotDotDotToken: boolean;
  readonly initializer?: Expression;
}

export interface ArrayBindingPattern extends Node {
  readonly kind: SyntaxKind.ArrayBindingPattern;
  readonly elements: (BindingElement | undefined)[];
}

export interface ObjectBindingPattern extends Node {
  readonly kind: SyntaxKind.ObjectBindingPattern;
  readonly elements: BindingElement[];
}

export interface TypeParameterDeclaration extends Node {
  readonly kind: SyntaxKind.TypeParameterDeclaration;
  readonly name: Identifier;
  readonly constraint?: TypeNode;
  readonly default?: TypeNode;
}

export interface FunctionDeclaration extends Node {
  readonly kind: SyntaxKind.FunctionDeclaration;
  readonly name?: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body?: Block;
  readonly flags: NodeFlags;
}

export interface HeritageClause extends Node {
  readonly kind: SyntaxKind.HeritageClause;
  readonly token: "extends" | "implements";
  readonly types: ExpressionWithTypeArguments[];
}

export interface ExpressionWithTypeArguments extends Node {
  readonly kind: SyntaxKind.ExpressionWithTypeArguments;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
}

export type ClassElement = PropertyDeclaration | MethodDeclaration | ConstructorDeclaration;

export interface PropertyDeclaration extends Node {
  readonly kind: SyntaxKind.PropertyDeclaration;
  readonly name: PropertyName;
  readonly modifiers: Modifier[];
  readonly questionToken: boolean;
  readonly exclamationToken: boolean;
  readonly type?: TypeNode;
  readonly initializer?: Expression;
}

export interface MethodDeclaration extends Node {
  readonly kind: SyntaxKind.MethodDeclaration;
  readonly name: PropertyName;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body?: Block;
  readonly flags: NodeFlags;
  readonly optional: boolean;
  /** Present for `get x()` / `set x(v)` accessors. */
  readonly accessor?: "get" | "set";
}

export interface ConstructorDeclaration extends Node {
  readonly kind: SyntaxKind.ConstructorDeclaration;
  readonly modifiers: Modifier[];
  readonly parameters: Parameter[];
  readonly body?: Block;
}

export interface ClassDeclaration extends Node {
  readonly kind: SyntaxKind.ClassDeclaration;
  readonly name?: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: ClassElement[];
}

export interface InterfaceDeclaration extends Node {
  readonly kind: SyntaxKind.InterfaceDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: TypeElement[];
}

export interface TypeAliasDeclaration extends Node {
  readonly kind: SyntaxKind.TypeAliasDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly typeParameters: TypeParameterDeclaration[];
  readonly type: TypeNode;
}

export interface EnumMember extends Node {
  readonly kind: SyntaxKind.EnumMember;
  readonly name: PropertyName;
  readonly initializer?: Expression;
}

export interface EnumDeclaration extends Node {
  readonly kind: SyntaxKind.EnumDeclaration;
  readonly name: Identifier;
  readonly modifiers: Modifier[];
  readonly members: EnumMember[];
}

export interface ModuleDeclaration extends Node {
  readonly kind: SyntaxKind.ModuleDeclaration;
  readonly name: Identifier | StringLiteral;
  readonly modifiers: Modifier[];
  readonly body?: Block;
}
