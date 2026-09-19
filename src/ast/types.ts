/**
 * Type nodes of the xbintsc AST. The parser records type syntax so diagnostics
 * and tooling can see it, but the code generator erases types before lowering.
 */

import { SyntaxKind } from "./kinds.js";
import type { TypeOperator } from "./operators.js";
import type { Node, TypeNode } from "./common.js";
import type {
  BooleanLiteral,
  Identifier,
  NullLiteral,
  NumericLiteral,
  StringLiteral,
  ThisExpression,
  PropertyName,
} from "./expressions.js";
import type { Parameter, TypeParameterDeclaration } from "./declarations.js";

// ---------------------------------------------------------------------------
// Type nodes
// ---------------------------------------------------------------------------

export interface TypeReference extends Node {
  readonly kind: SyntaxKind.TypeReference;
  readonly typeName: Identifier | QualifiedName;
  readonly typeArguments: TypeNode[];
}

export interface QualifiedName extends Node {
  readonly kind: SyntaxKind.QualifiedName;
  readonly left: Identifier | QualifiedName;
  readonly right: Identifier;
}

export interface TypePredicate extends Node {
  readonly kind: SyntaxKind.TypePredicate;
  readonly parameterName: Identifier | ThisExpression;
  readonly type?: TypeNode;
}

export interface KeywordTypeNode extends Node {
  readonly kind:
    | SyntaxKind.AnyKeywordType
    | SyntaxKind.UnknownKeywordType
    | SyntaxKind.NumberKeywordType
    | SyntaxKind.BigIntKeywordType
    | SyntaxKind.StringKeywordType
    | SyntaxKind.BooleanKeywordType
    | SyntaxKind.SymbolKeywordType
    | SyntaxKind.VoidKeywordType
    | SyntaxKind.UndefinedKeywordType
    | SyntaxKind.NullKeywordType
    | SyntaxKind.NeverKeywordType
    | SyntaxKind.ObjectKeywordType
    | SyntaxKind.ThisKeywordType;
}

export interface LiteralTypeNode extends Node {
  readonly kind: SyntaxKind.LiteralType;
  readonly literal: StringLiteral | NumericLiteral | BooleanLiteral | NullLiteral;
}

export interface ArrayTypeNode extends Node {
  readonly kind: SyntaxKind.ArrayType;
  readonly elementType: TypeNode;
}

export interface TupleTypeNode extends Node {
  readonly kind: SyntaxKind.TupleType;
  readonly elements: TypeNode[];
}

export interface UnionTypeNode extends Node {
  readonly kind: SyntaxKind.UnionType;
  readonly types: TypeNode[];
}

export interface IntersectionTypeNode extends Node {
  readonly kind: SyntaxKind.IntersectionType;
  readonly types: TypeNode[];
}

export interface FunctionTypeNode extends Node {
  readonly kind: SyntaxKind.FunctionType;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType: TypeNode;
}

export type TypeElement = PropertySignature | MethodSignature | IndexSignature;

export interface PropertySignature extends Node {
  readonly kind: SyntaxKind.PropertySignature;
  readonly name: PropertyName;
  readonly questionToken: boolean;
  readonly readonlyToken: boolean;
  readonly type?: TypeNode;
}

export interface MethodSignature extends Node {
  readonly kind: SyntaxKind.MethodSignature;
  readonly name: PropertyName;
  readonly questionToken: boolean;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
}

export interface IndexSignature extends Node {
  readonly kind: SyntaxKind.IndexSignature;
  readonly parameters: Parameter[];
  readonly type?: TypeNode;
}

export interface TypeLiteralNode extends Node {
  readonly kind: SyntaxKind.TypeLiteral;
  readonly members: TypeElement[];
}

export interface ParenthesizedTypeNode extends Node {
  readonly kind: SyntaxKind.ParenthesizedType;
  readonly type: TypeNode;
}

export interface TypeQueryNode extends Node {
  readonly kind: SyntaxKind.TypeQuery;
  readonly exprName: Identifier | QualifiedName;
  readonly typeArguments: TypeNode[];
}

export interface IndexedAccessTypeNode extends Node {
  readonly kind: SyntaxKind.IndexedAccessType;
  readonly objectType: TypeNode;
  readonly indexType: TypeNode;
}

export interface TypeOperatorNode extends Node {
  readonly kind: SyntaxKind.TypeOperator;
  readonly operator: TypeOperator;
  readonly type: TypeNode;
}

export interface MappedTypeNode extends Node {
  readonly kind: SyntaxKind.MappedType;
  readonly readonlyToken: boolean | "+" | "-";
  readonly typeParameter: TypeParameterDeclaration;
  readonly questionToken: boolean | "+" | "-";
  readonly type?: TypeNode;
}

export interface ConditionalTypeNode extends Node {
  readonly kind: SyntaxKind.ConditionalType;
  readonly checkType: TypeNode;
  readonly extendsType: TypeNode;
  readonly trueType: TypeNode;
  readonly falseType: TypeNode;
}

export interface InferTypeNode extends Node {
  readonly kind: SyntaxKind.InferType;
  readonly typeParameter: TypeParameterDeclaration;
}

export interface OptionalTypeNode extends Node {
  readonly kind: SyntaxKind.OptionalType;
  readonly type: TypeNode;
}

export interface RestTypeNode extends Node {
  readonly kind: SyntaxKind.RestType;
  readonly type: TypeNode;
}
