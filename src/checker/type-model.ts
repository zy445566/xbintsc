/**
 * The value model used by the checker: a deliberately small structural type
 * union plus the helpers shared by resolution and assignability.
 */

import {
  ModifierKind,
  SyntaxKind,
  type Expression,
  type Identifier,
  type Parameter,
} from "../ast/nodes.js";

export type PrimitiveName =
  | "number"
  | "string"
  | "boolean"
  | "bigint"
  | "symbol"
  | "null"
  | "undefined"
  | "void"
  | "object";

export interface PropertyInfo {
  readonly type: Type;
  readonly optional: boolean;
}

export interface ParameterInfo {
  readonly type: Type;
  readonly optional: boolean;
  readonly rest: boolean;
}

export interface TypeParameterInfo {
  readonly name: string;
  readonly constraint?: Type;
}

export type Type =
  | { readonly kind: "any" }
  | { readonly kind: "primitive"; readonly name: PrimitiveName }
  | { readonly kind: "literal"; readonly value: string | number | boolean; readonly primitive: "string" | "number" | "boolean" }
  | { readonly kind: "array"; readonly element: Type }
  | { readonly kind: "tuple"; readonly elements: readonly Type[] }
  | {
      readonly kind: "object";
      readonly properties: ReadonlyMap<string, PropertyInfo>;
      readonly index?: Type;
      /** False when the shape is open (index signature, `object`, ...). */
      readonly closed: boolean;
      readonly label?: string;
    }
  | {
      readonly kind: "function";
      readonly parameters: readonly ParameterInfo[];
      readonly returnType: Type;
      readonly typeParameters: readonly TypeParameterInfo[];
      readonly label?: string;
    }
  | { readonly kind: "union"; readonly members: readonly Type[] }
  | { readonly kind: "typeParam"; readonly name: string };

export const ANY: Type = { kind: "any" };
export const UNKNOWN: Type = { kind: "any" }; // no strict unknown mode: treat like any

/** Own properties every object inherits from `Object.prototype`. */
export const OBJECT_PROTOTYPE_NAMES: ReadonlySet<string> = new Set([
  "constructor",
  "hasOwnProperty",
  "isPrototypeOf",
  "propertyIsEnumerable",
  "toLocaleString",
  "toString",
  "valueOf",
  "__defineGetter__",
  "__defineSetter__",
  "__lookupGetter__",
  "__lookupSetter__",
  "__proto__",
]);

export function isAny(type: Type): boolean {
  return type.kind === "any" || type.kind === "typeParam";
}

export function primitive(name: PrimitiveName): Type {
  return { kind: "primitive", name };
}

export function unionOf(members: readonly Type[]): Type {
  const flattened: Type[] = [];
  for (const member of members) {
    if (member.kind === "any" || member.kind === "typeParam") return ANY;
    if (member.kind === "union") flattened.push(...member.members);
    else flattened.push(member);
  }
  const unique: Type[] = [];
  for (const member of flattened) {
    if (!unique.some((other) => sameType(member, other))) unique.push(member);
  }
  if (unique.length === 0) return ANY;
  if (unique.length === 1) return unique[0]!;
  return { kind: "union", members: unique };
}

export function unionMembers(type: Type): readonly Type[] {
  return type.kind === "union" ? type.members : [type];
}

/**
 * Widen the literal types TypeScript widens for a mutable position: `let x =
 * 1` and object/array literal members are `number`, not the literal `1`.
 * (`const x = 1` keeps the literal; the caller decides when to widen.)
 */
export function widenType(type: Type): Type {
  switch (type.kind) {
    case "literal":
      return primitive(type.primitive);
    case "array":
      return { kind: "array", element: widenType(type.element) };
    case "tuple":
      return { kind: "tuple", elements: type.elements.map((element) => widenType(element)) };
    case "union":
      return unionOf(type.members.map((member) => widenType(member)));
    case "object": {
      const properties = new Map<string, PropertyInfo>();
      for (const [name, property] of type.properties) {
        properties.set(name, { type: widenType(property.type), optional: property.optional });
      }
      return {
        kind: "object",
        properties,
        ...(type.index ? { index: widenType(type.index) } : {}),
        closed: type.closed,
        ...(type.label ? { label: type.label } : {}),
      };
    }
    default:
      return type;
  }
}

function sameType(a: Type, b: Type): boolean {
  if (a.kind !== b.kind) return false;
  if (a.kind === "primitive" && b.kind === "primitive") return a.name === b.name;
  if (a.kind === "literal" && b.kind === "literal") return a.value === b.value;
  if (a.kind === "typeParam" && b.kind === "typeParam") return a.name === b.name;
  return false;
}

export function printType(type: Type, depth = 0): string {
  if (depth > 3) return "...";
  switch (type.kind) {
    case "any":
      return "any";
    case "primitive":
      return type.name;
    case "literal":
      return typeof type.value === "string" ? JSON.stringify(type.value) : String(type.value);
    case "array":
      return `${printType(type.element, depth + 1)}[]`;
    case "tuple":
      return `[${type.elements.map((element) => printType(element, depth + 1)).join(", ")}]`;
    case "union":
      return type.members.map((member) => printType(member, depth + 1)).join(" | ");
    case "function": {
      const params = type.parameters.map((parameter) => `${parameter.rest ? "..." : ""}${printType(parameter.type, depth + 1)}${parameter.optional ? "?" : ""}`);
      return `(${params.join(", ")}) => ${printType(type.returnType, depth + 1)}`;
    }
    case "typeParam":
      return type.name;
    case "object": {
      if (type.label) return type.label;
      const entries = [...type.properties.entries()].slice(0, 6).map(([name, property]) => `${name}${property.optional ? "?" : ""}: ${printType(property.type, depth + 1)}`);
      if (type.properties.size > 6) entries.push("...");
      return `{ ${entries.join("; ")} }`;
    }
  }
}

/** Shared unwrapping for erased expression wrappers. */
export function unwrap(expression: Expression): Expression {
  let current = expression;
  for (;;) {
    if (
      current.kind === SyntaxKind.AsExpression ||
      current.kind === SyntaxKind.SatisfiesExpression ||
      current.kind === SyntaxKind.NonNullExpression ||
      current.kind === SyntaxKind.ParenthesizedExpression
    ) {
      current = (current as unknown as { expression: Expression }).expression;
      continue;
    }
    return current;
  }
}

export function isThisParameter(parameter: Parameter): boolean {
  return parameter.name.kind === SyntaxKind.Identifier && (parameter.name as Identifier).text === "this";
}

export function isParameterProperty(parameter: Parameter): boolean {
  return parameter.modifiers.some(
    (modifier) =>
      modifier.modifierKind === ModifierKind.Public ||
      modifier.modifierKind === ModifierKind.Private ||
      modifier.modifierKind === ModifierKind.Protected ||
      modifier.modifierKind === ModifierKind.Readonly,
  );
}
