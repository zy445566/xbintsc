/**
 * Lightweight control-flow narrowing.
 *
 * Only the predicates whose refinement is unambiguous are handled:
 * `typeof x === "..."`, comparisons with `null`/`undefined`, `instanceof`,
 * truthiness and the `&&` / `||` / `??` forms that combine them. Everything
 * else leaves the flow untouched, so a missed narrowing can only hide an
 * error, never invent one.
 */

import {
  SyntaxKind,
  type BinaryExpression,
  type Expression,
  type Identifier,
  type PrefixUnaryExpression,
  type TypeOfExpression,
} from "../ast/nodes.js";
import { primitive, unionMembers, unwrap, type Type } from "./type-model.js";
import type { TypeResolver } from "./type-system.js";

export class Flow {
  constructor(
    readonly parent?: Flow,
    readonly overrides: ReadonlyMap<number, Type> = new Map(),
  ) {}

  typeOf(symbolId: number): Type | undefined {
    return this.overrides.get(symbolId) ?? this.parent?.typeOf(symbolId);
  }

  with(overrides: ReadonlyMap<number, Type>): Flow {
    return new Flow(this, overrides);
  }
}

function merge(...maps: ReadonlyMap<number, Type>[]): Map<number, Type> {
  const result = new Map<number, Type>();
  for (const map of maps) for (const [key, value] of map) result.set(key, value);
  return result;
}

function symbolIdOf(expression: Expression, resolver: TypeResolver): number | undefined {
  const unwrapped = unwrap(expression);
  if (unwrapped.kind !== SyntaxKind.Identifier) return undefined;
  return resolver.binding.symbolOfIdentifier.get(unwrapped as Identifier)?.id;
}

function nullishKind(expression: Expression): "null" | "undefined" | "nullish" | undefined {
  const unwrapped = unwrap(expression);
  if (unwrapped.kind === SyntaxKind.NullKeyword) return "null";
  if (unwrapped.kind === SyntaxKind.UndefinedKeyword) return "undefined";
  if (unwrapped.kind === SyntaxKind.Identifier && (unwrapped as Identifier).text === "undefined") return "undefined";
  return undefined;
}

function literalType(expression: Expression): Type | undefined {
  const unwrapped = unwrap(expression);
  switch (unwrapped.kind) {
    case SyntaxKind.StringLiteral:
    case SyntaxKind.NoSubstitutionTemplateLiteral:
      return { kind: "literal", value: (unwrapped as { value: string }).value, primitive: "string" };
    case SyntaxKind.NumericLiteral:
      return { kind: "literal", value: (unwrapped as { value: number }).value, primitive: "number" };
    case SyntaxKind.TrueKeyword:
      return { kind: "literal", value: true, primitive: "boolean" };
    case SyntaxKind.FalseKeyword:
      return { kind: "literal", value: false, primitive: "boolean" };
    default:
      return undefined;
  }
}

const TYPEOF_NAMES: Readonly<Record<string, "string" | "number" | "boolean" | "bigint" | "symbol" | "undefined">> = {
  string: "string",
  number: "number",
  boolean: "boolean",
  bigint: "bigint",
  symbol: "symbol",
  undefined: "undefined",
};

function filterType(type: Type, predicate: (member: Type) => boolean): Type | undefined {
  const kept = unionMembers(type).filter(predicate);
  if (kept.length === 0) return undefined;
  if (kept.length === 1) return kept[0]!;
  return { kind: "union", members: kept };
}

export function withoutNullish(type: Type): Type {
  return filterType(type, (member) => !(member.kind === "primitive" && (member.name === "null" || member.name === "undefined"))) ?? type;
}

function refine(type: Type, predicate: (member: Type) => boolean): Type | undefined {
  return filterType(type, predicate);
}

function positiveRefinement(expression: Expression, flow: Flow, resolver: TypeResolver): Map<number, Type> {
  const result = new Map<number, Type>();
  const unwrapped = unwrap(expression);

  if (unwrapped.kind === SyntaxKind.Identifier) {
    const id = symbolIdOf(unwrapped, resolver);
    if (id !== undefined) {
      const type = flow.typeOf(id) ?? resolver.typeOfSymbolId(id);
      if (type) result.set(id, withoutNullish(type));
    }
    return result;
  }

  if (unwrapped.kind === SyntaxKind.PrefixUnaryExpression) {
    const unary = unwrapped as PrefixUnaryExpression;
    if (unary.operator === "!") return negativeRefinement(unary.operand, flow, resolver);
    return result;
  }

  if (unwrapped.kind !== SyntaxKind.BinaryExpression) return result;
  const binary = unwrapped as BinaryExpression;
  const operator = binary.operator;

  if (operator === "&&") return merge(positiveRefinement(binary.left, flow, resolver), positiveRefinement(binary.right, flow, resolver));
  if (operator === "||") {
    // `a || b` is truthy when either side is; no single refinement to apply.
    return result;
  }
  if (operator === "??") return positiveRefinement(binary.left, flow, resolver);

  const equality = operator === "===" || operator === "==";
  const inequality = operator === "!==" || operator === "!=";
  if (equality || inequality) {
    const leftNullish = nullishKind(binary.left);
    const rightNullish = nullishKind(binary.right);
    const nullish = leftNullish ?? rightNullish;
    const other = leftNullish ? binary.right : binary.left;
    const id = symbolIdOf(other, resolver);
    if (id !== undefined && nullish) {
      if (equality) {
        const target = nullish === "nullish" ? [primitive("null"), primitive("undefined")] : [primitive(nullish)];
        for (const type of target) result.set(id, type);
      } else {
        const base = flow.typeOf(id) ?? resolver.typeOfSymbolId(id);
        if (base) {
          const narrowed = refine(base, (member) => !(member.kind === "primitive" && (member.name === "null" || member.name === "undefined")));
          if (narrowed) result.set(id, narrowed);
        }
      }
      return result;
    }
    // `typeof x === "string"` and friends.
    const typeofSide = unwrap(binary.left).kind === SyntaxKind.TypeOfExpression ? binary.left : unwrap(binary.right).kind === SyntaxKind.TypeOfExpression ? binary.right : undefined;
    const literalSide = literalType(typeofSide === binary.left ? binary.right : binary.left);
    if (typeofSide && literalSide?.kind === "literal" && typeof literalSide.value === "string") {
      const name = TYPEOF_NAMES[literalSide.value];
      const operand = (unwrap(typeofSide) as TypeOfExpression).expression;
      const operandId = symbolIdOf(operand, resolver);
      if (name && operandId !== undefined) {
        const base = flow.typeOf(operandId) ?? resolver.typeOfSymbolId(operandId);
        if (base) {
          const narrowed = refine(base, (member) => matchesTypeof(member, name));
          if (narrowed) result.set(operandId, narrowed);
        }
      }
      return result;
    }
    // `x === "literal"` narrows to that literal.
    const leftLiteral = literalType(binary.left);
    const rightLiteral = literalType(binary.right);
    if (leftLiteral && !rightLiteral) {
      const id = symbolIdOf(binary.right, resolver);
      if (id !== undefined) result.set(id, leftLiteral);
    } else if (rightLiteral && !leftLiteral) {
      const id = symbolIdOf(binary.left, resolver);
      if (id !== undefined) result.set(id, rightLiteral);
    }
    return result;
  }

  if (operator === "instanceof") {
    const id = symbolIdOf(binary.left, resolver);
    if (id === undefined) return result;
    const instance = resolver.classInstanceFromIdentifier(binary.right);
    if (instance) result.set(id, instance);
  }
  return result;
}

function negativeRefinement(expression: Expression, flow: Flow, resolver: TypeResolver): Map<number, Type> {
  const result = new Map<number, Type>();
  const unwrapped = unwrap(expression);
  if (unwrapped.kind === SyntaxKind.PrefixUnaryExpression) {
    const unary = unwrapped as PrefixUnaryExpression;
    if (unary.operator === "!") return positiveRefinement(unary.operand, flow, resolver);
    return result;
  }
  if (unwrapped.kind !== SyntaxKind.BinaryExpression) return result;
  const binary = unwrapped as BinaryExpression;
  if (binary.operator === "&&") {
    // `!(a && b)` does not refine either side on its own.
    return result;
  }
  if (binary.operator === "||") {
    return merge(negativeRefinement(binary.left, flow, resolver), negativeRefinement(binary.right, flow, resolver));
  }
  const equality = binary.operator === "===" || binary.operator === "==";
  const inequality = binary.operator === "!==" || binary.operator === "!=";
  if (!equality && !inequality) return result;
  const leftNullish = nullishKind(binary.left);
  const rightNullish = nullishKind(binary.right);
  const nullish = leftNullish ?? rightNullish;
  const other = leftNullish ? binary.right : binary.left;
  const id = symbolIdOf(other, resolver);
  if (id === undefined || !nullish) return result;
  if (equality) {
    // `!(x === null)` means x is non-null.
    const base = flow.typeOf(id) ?? resolver.typeOfSymbolId(id);
    if (!base) return result;
    const narrowed = refine(base, (member) => !(member.kind === "primitive" && (member.name === "null" || member.name === "undefined")));
    if (narrowed) result.set(id, narrowed);
  } else {
    // `!(x !== null)` means x is null (or nullish for `!= null`).
    const target = nullish === "nullish" ? [primitive("null"), primitive("undefined")] : [primitive(nullish)];
    for (const type of target) result.set(id, type);
  }
  return result;
}

function matchesTypeof(type: Type, name: string): boolean {
  if (type.kind === "literal") return type.primitive === name;
  if (type.kind !== "primitive") return false;
  return type.name === name;
}

export function narrow(condition: Expression, truthy: boolean, flow: Flow, resolver: TypeResolver): Flow {
  const overrides = truthy ? positiveRefinement(condition, flow, resolver) : negativeRefinement(condition, flow, resolver);
  return overrides.size > 0 ? flow.with(overrides) : flow;
}
