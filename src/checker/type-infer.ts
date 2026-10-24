/**
 * Inference from expressions into the checker's type model.
 *
 * It is deliberately shallow: literals, object/array literals, `new`, function
 * expressions and the obvious operators are inferred; calls, property reads and
 * everything else are `any` here (the type-check walk refines them with flow).
 */

import {
  SyntaxKind,
  type ArrowFunction,
  type ArrayLiteralExpression,
  type AsExpression,
  type BinaryExpression,
  type ClassDeclaration,
  type ConditionalExpression,
  type Expression,
  type FunctionExpression,
  type Identifier,
  type NewExpression,
  type ObjectLiteralExpression,
  type Parameter,
  type ShorthandPropertyAssignment,
  type TypeNode,
} from "../ast/nodes.js";
import { SymbolKind, type BindResult, type SymbolInfo } from "../binder/binder.js";
import { classMemberName } from "../binder/helpers.js";
import { ASSIGNMENT_OPERATORS } from "../codegen/generator/tables.js";
import { ANY, isAny, isThisParameter, primitive, unionOf, widenType, type ParameterInfo, type PropertyInfo, type Type } from "./type-model.js";

export interface InferenceContext {
  readonly binding: BindResult;
  resolve(node: TypeNode | undefined, typeParams?: ReadonlyMap<string, Type>): Type;
  typeOfSymbol(symbol: SymbolInfo): Type;
  classInstanceType(node: ClassDeclaration): Type;
  parameterInfo(parameter: Parameter, typeParams: ReadonlyMap<string, Type>): ParameterInfo;
}

export function inferTypeOfExpression(
  context: InferenceContext,
  expression: Expression,
  lookup?: (symbol: SymbolInfo) => Type | undefined,
): Type {
    switch (expression.kind) {
      case SyntaxKind.NumericLiteral:
        return { kind: "literal", value: (expression as { value: number }).value, primitive: "number" };
      case SyntaxKind.StringLiteral:
      case SyntaxKind.NoSubstitutionTemplateLiteral:
        return { kind: "literal", value: (expression as { value: string }).value, primitive: "string" };
      case SyntaxKind.TrueKeyword:
        return { kind: "literal", value: true, primitive: "boolean" };
      case SyntaxKind.FalseKeyword:
        return { kind: "literal", value: false, primitive: "boolean" };
      case SyntaxKind.NullKeyword:
        return primitive("null");
      case SyntaxKind.UndefinedKeyword:
        return primitive("undefined");
      case SyntaxKind.BigIntLiteral:
        return primitive("bigint");
      case SyntaxKind.Identifier: {
        const symbol = context.binding.symbolOfIdentifier.get(expression as Identifier);
        if (!symbol) return ANY;
        return lookup?.(symbol) ?? context.typeOfSymbol(symbol);
      }
      case SyntaxKind.ObjectLiteralExpression:
        return objectLiteralType(context, expression as ObjectLiteralExpression);
      case SyntaxKind.ArrayLiteralExpression: {
        const elements = (expression as ArrayLiteralExpression).elements.map((element) => {
          if (element.kind !== SyntaxKind.SpreadElement) return inferTypeOfExpression(context, element, lookup);
          const spread = inferTypeOfExpression(context, (element as unknown as { expression: Expression }).expression, lookup);
          return spread.kind === "array" ? spread.element : ANY;
        });
        return { kind: "array", element: widenType(unionOf(elements)) };
      }
      case SyntaxKind.NewExpression: {
        const call = expression as NewExpression;
        if (call.expression.kind !== SyntaxKind.Identifier) return ANY;
        const symbol = context.binding.symbolOfIdentifier.get(call.expression as Identifier);
        if (!symbol || symbol.kind !== SymbolKind.Class || symbol.declarations.length !== 1) return ANY;
        const declaration = symbol.declarations[0]!;
        if (declaration.kind !== SyntaxKind.ClassDeclaration) return ANY;
        return context.classInstanceType(declaration as ClassDeclaration);
      }
      case SyntaxKind.ArrowFunction:
      case SyntaxKind.FunctionExpression:
        return functionExpressionType(context, expression as ArrowFunction | FunctionExpression);
      case SyntaxKind.AsExpression:
        return context.resolve((expression as AsExpression).type);
      case SyntaxKind.SatisfiesExpression:
      case SyntaxKind.ParenthesizedExpression:
      case SyntaxKind.NonNullExpression:
        return inferTypeOfExpression(context, (expression as unknown as { expression: Expression }).expression, lookup);
      case SyntaxKind.TypeOfExpression:
        return primitive("string");
      case SyntaxKind.VoidExpression:
        return primitive("undefined");
      case SyntaxKind.TemplateLiteral:
        return primitive("string");
      case SyntaxKind.ConditionalExpression: {
        const conditional = expression as ConditionalExpression;
        return unionOf([inferTypeOfExpression(context, conditional.whenTrue, lookup), inferTypeOfExpression(context, conditional.whenFalse, lookup)]);
      }
      case SyntaxKind.BinaryExpression: {
        const binary = expression as BinaryExpression;
        if (binary.operator === "+") {
          const left = inferTypeOfExpression(context, binary.left, lookup);
          const right = inferTypeOfExpression(context, binary.right, lookup);
          if (isAny(left) || isAny(right)) return ANY;
          if (isBigIntish(left) || isBigIntish(right)) return primitive("bigint");
          if (isStringish(left) || isStringish(right)) return primitive("string");
          return primitive("number");
        }
        if (ASSIGNMENT_OPERATORS.has(binary.operator)) return ANY;
        if (["<", "<=", ">", ">=", "==", "!=", "===", "!==", "in", "instanceof"].includes(binary.operator)) return primitive("boolean");
        if (binary.operator === "&&" || binary.operator === "||" || binary.operator === "??") {
          return unionOf([inferTypeOfExpression(context, binary.left, lookup), inferTypeOfExpression(context, binary.right, lookup)]);
        }
        const left = inferTypeOfExpression(context, binary.left, lookup);
        const right = inferTypeOfExpression(context, binary.right, lookup);
        if (isAny(left) || isAny(right)) return ANY;
        if (isBigIntish(left) || isBigIntish(right)) return primitive("bigint");
        return primitive("number");
      }
      case SyntaxKind.PrefixUnaryExpression: {
        const operator = (expression as { operator: string }).operator;
        if (operator === "typeof") return primitive("string");
        if (operator === "!") return primitive("boolean");
        if (operator === "void") return primitive("undefined");
        return primitive("number");
      }
      case SyntaxKind.PostfixUnaryExpression:
        return primitive("number");
      case SyntaxKind.CallExpression:
        return ANY;
      case SyntaxKind.PropertyAccessExpression:
        return ANY;
      default:
        return ANY;
    }
  }

function objectLiteralType(context: InferenceContext, literal: ObjectLiteralExpression): Type {
    const properties = new Map<string, PropertyInfo>();
    for (const element of literal.properties) {
      if (element.kind === SyntaxKind.PropertyAssignment) {
        const property = element as { name: Parameters<typeof classMemberName>[0]; initializer: Expression; accessor?: "get" | "set" };
        const name = classMemberName(property.name);
        if (!name) return ANY;
        // A getter/setter pair is a value property, not a function property;
        // its exact type is not inferred here.
        const type = property.accessor ? ANY : widenType(inferTypeOfExpression(context, property.initializer));
        if (!(property.accessor === "set" && properties.has(name))) properties.set(name, { type, optional: false });
      } else if (element.kind === SyntaxKind.ShorthandPropertyAssignment) {
        const shorthand = element as ShorthandPropertyAssignment;
        properties.set(shorthand.name.text, { type: inferTypeOfExpression(context, shorthand.name), optional: false });
      } else {
        return ANY; // spread: the shape is not static
      }
    }
    return { kind: "object", properties, closed: true };
  }

function functionExpressionType(context: InferenceContext, expression: ArrowFunction | FunctionExpression): Type {
    const typeParams = new Map<string, Type>();
    return {
      kind: "function",
      parameters: expression.parameters.filter((parameter) => !isThisParameter(parameter)).map((parameter) => context.parameterInfo(parameter, typeParams)),
      returnType: context.resolve(expression.returnType, typeParams),
      typeParameters: (expression.typeParameters ?? []).map((parameter) => ({ name: parameter.name.text })),
    };
  }

function isStringish(type: Type): boolean {
  if (type.kind === "primitive") return type.name === "string";
  if (type.kind === "literal") return type.primitive === "string";
  return false;
}

function isBigIntish(type: Type): boolean {
  return (type.kind === "primitive" && type.name === "bigint");
}
