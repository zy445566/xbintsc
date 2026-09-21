/**
 * Expression nodes of the xbintsc AST, plus the literal and identifier nodes
 * they are built from.
 */

import type { Token } from "../lexer/token.js";
import { NodeFlags, SyntaxKind } from "./kinds.js";
import {
  AssignmentOperator,
  BinaryOperator,
  PostfixUnaryOperator,
  PrefixUnaryOperator,
} from "./operators.js";
import type { Expression, Node, TypeNode } from "./common.js";
import type { Block } from "./statements.js";
import type { ClassElement, HeritageClause, Parameter, TypeParameterDeclaration } from "./declarations.js";

// ---------------------------------------------------------------------------
// Identifiers and literals
// ---------------------------------------------------------------------------

export interface Identifier extends Node {
  readonly kind: SyntaxKind.Identifier;
  readonly text: string;
  /** True when the identifier was written with a `#` prefix. */
  readonly escaped?: boolean;
}

export interface PrivateIdentifier extends Node {
  readonly kind: SyntaxKind.PrivateIdentifier;
  readonly text: string;
}

export interface NumericLiteral extends Node {
  readonly kind: SyntaxKind.NumericLiteral;
  readonly text: string;
  readonly value: number;
}

export interface BigIntLiteral extends Node {
  readonly kind: SyntaxKind.BigIntLiteral;
  readonly text: string;
  readonly value: number;
}

export interface StringLiteral extends Node {
  readonly kind: SyntaxKind.StringLiteral;
  readonly text: string;
  readonly value: string;
  /** Raw source text, including quotes. */
  readonly raw: string;
}

export interface NoSubstitutionTemplateLiteral extends Node {
  readonly kind: SyntaxKind.NoSubstitutionTemplateLiteral;
  readonly text: string;
  readonly value: string;
}

export interface TemplateLiteral extends Node {
  readonly kind: SyntaxKind.TemplateLiteral;
  readonly head: string;
  readonly spans: TemplateSpan[];
}

export interface TemplateSpan extends Node {
  readonly kind: SyntaxKind.TemplateSpan;
  readonly expression: Expression;
  readonly literal: string;
  /** True when this span terminates the template (tail) rather than continues it. */
  readonly isTail: boolean;
}

export interface RegularExpressionLiteral extends Node {
  readonly kind: SyntaxKind.RegularExpressionLiteral;
  readonly text: string;
  readonly pattern: string;
  readonly flags: string;
}

export interface BooleanLiteral extends Node {
  readonly kind: SyntaxKind.TrueKeyword | SyntaxKind.FalseKeyword;
  readonly value: boolean;
}

export interface NullLiteral extends Node {
  readonly kind: SyntaxKind.NullKeyword;
}

export interface UndefinedLiteral extends Node {
  readonly kind: SyntaxKind.UndefinedKeyword;
}

export interface ThisExpression extends Node {
  readonly kind: SyntaxKind.ThisKeyword;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

export interface ArrayLiteralExpression extends Node {
  readonly kind: SyntaxKind.ArrayLiteralExpression;
  readonly elements: Expression[];
}

export interface PropertyAssignment extends Node {
  readonly kind: SyntaxKind.PropertyAssignment;
  readonly name: PropertyName;
  readonly initializer: Expression;
}

export interface ShorthandPropertyAssignment extends Node {
  readonly kind: SyntaxKind.ShorthandPropertyAssignment;
  readonly name: Identifier;
  /** Present for `{ a = default }` when used as a destructuring target. */
  readonly initializer?: Expression;
}

export interface SpreadElement extends Node {
  readonly kind: SyntaxKind.SpreadElement;
  readonly expression: Expression;
}

export type ObjectLiteralElementLike = PropertyAssignment | ShorthandPropertyAssignment | SpreadElement;

export interface ObjectLiteralExpression extends Node {
  readonly kind: SyntaxKind.ObjectLiteralExpression;
  readonly properties: ObjectLiteralElementLike[];
}

export type PropertyName = Identifier | StringLiteral | NumericLiteral | PrivateIdentifier | ComputedPropertyName;

export interface ComputedPropertyName extends Node {
  readonly kind: SyntaxKind.ComputedPropertyName;
  readonly expression: Expression;
}

export interface FunctionExpression extends Node {
  readonly kind: SyntaxKind.FunctionExpression;
  readonly name?: Identifier;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body: Block;
  readonly flags: NodeFlags;
}

export interface ArrowFunction extends Node {
  readonly kind: SyntaxKind.ArrowFunction;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly parameters: Parameter[];
  readonly returnType?: TypeNode;
  readonly body: Block | Expression;
  readonly flags: NodeFlags;
}

export interface ClassExpression extends Node {
  readonly kind: SyntaxKind.ClassExpression;
  readonly name?: Identifier;
  readonly typeParameters: TypeParameterDeclaration[];
  readonly heritage: HeritageClause[];
  readonly members: ClassElement[];
}

export interface CallExpression extends Node {
  readonly kind: SyntaxKind.CallExpression;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
  readonly arguments: Expression[];
  readonly optional: boolean;
}

export interface NewExpression extends Node {
  readonly kind: SyntaxKind.NewExpression;
  readonly expression: Expression;
  readonly typeArguments: TypeNode[];
  readonly arguments: Expression[];
}

export interface PropertyAccessExpression extends Node {
  readonly kind: SyntaxKind.PropertyAccessExpression;
  readonly expression: Expression;
  readonly name: Identifier;
  readonly optional: boolean;
}

export interface ElementAccessExpression extends Node {
  readonly kind: SyntaxKind.ElementAccessExpression;
  readonly expression: Expression;
  readonly argumentExpression: Expression;
  readonly optional: boolean;
}

export interface ParenthesizedExpression extends Node {
  readonly kind: SyntaxKind.ParenthesizedExpression;
  readonly expression: Expression;
}

export interface PrefixUnaryExpression extends Node {
  readonly kind: SyntaxKind.PrefixUnaryExpression;
  readonly operator: PrefixUnaryOperator;
  readonly operand: Expression;
}

export interface PostfixUnaryExpression extends Node {
  readonly kind: SyntaxKind.PostfixUnaryExpression;
  readonly operator: PostfixUnaryOperator;
  readonly operand: Expression;
}

export interface BinaryExpression extends Node {
  readonly kind: SyntaxKind.BinaryExpression;
  readonly left: Expression;
  readonly operator: BinaryOperator;
  readonly right: Expression;
  readonly operatorToken: Token;
}

export interface AssignmentExpression extends Node {
  readonly kind: SyntaxKind.BinaryExpression;
  readonly left: Expression;
  readonly operator: AssignmentOperator;
  readonly right: Expression;
  readonly operatorToken: Token;
}

export interface ConditionalExpression extends Node {
  readonly kind: SyntaxKind.ConditionalExpression;
  readonly condition: Expression;
  readonly whenTrue: Expression;
  readonly whenFalse: Expression;
}

export interface AsExpression extends Node {
  readonly kind: SyntaxKind.AsExpression;
  readonly expression: Expression;
  readonly type: TypeNode;
}

export interface SatisfiesExpression extends Node {
  readonly kind: SyntaxKind.SatisfiesExpression;
  readonly expression: Expression;
  readonly type: TypeNode;
}

export interface NonNullExpression extends Node {
  readonly kind: SyntaxKind.NonNullExpression;
  readonly expression: Expression;
}

export interface TypeOfExpression extends Node {
  readonly kind: SyntaxKind.TypeOfExpression;
  readonly expression: Expression;
}

export interface VoidExpression extends Node {
  readonly kind: SyntaxKind.VoidExpression;
  readonly expression?: Expression;
}

export interface DeleteExpression extends Node {
  readonly kind: SyntaxKind.DeleteExpression;
  readonly expression: Expression;
}

export interface AwaitExpression extends Node {
  readonly kind: SyntaxKind.AwaitExpression;
  readonly expression: Expression;
}

export interface YieldExpression extends Node {
  readonly kind: SyntaxKind.YieldExpression;
  readonly expression?: Expression;
  readonly delegate: boolean;
}

export interface TaggedTemplateExpression extends Node {
  readonly kind: SyntaxKind.TaggedTemplateExpression;
  readonly tag: Expression;
  readonly template: TemplateLiteral | NoSubstitutionTemplateLiteral;
}
