/**
 * Type-inference helpers used by the checker: generic substitution, array
 * element lookup and property lookup. They live on a prototype mixin so the
 * main walker stays within the repository's per-file line budget.
 */

import { SyntaxKind, type CallExpression } from "../ast/nodes.js";
import { ANY, OBJECT_PROTOTYPE_NAMES, unionMembers, unionOf, type Type } from "./type-model.js";
import type { Flow } from "./flow.js";
import type { CheckContext, TypeChecker } from "./typecheck.js";

export interface TypeCheckerMembers {
  inferTypeArguments(this: TypeChecker, callee: Extract<Type, { kind: "function" }>, call: CallExpression, flow: Flow, _context: CheckContext): Map<string, Type>;
  inferFromPattern(this: TypeChecker, pattern: Type, actual: Type, substitutions: Map<string, Type>, names: ReadonlySet<string>): void;
  substitute(this: TypeChecker, type: Type, substitutions: ReadonlyMap<string, Type>): Type;
  lookupProperty(this: TypeChecker, base: Type, name: string): { type: Type; missing: boolean };
  lookupIn(this: TypeChecker, type: Type, name: string): Type | undefined;
}

export const typeCheckerMembers: TypeCheckerMembers = {
  inferTypeArguments(callee: Extract<Type, { kind: "function" }>, call: CallExpression, flow: Flow, _context: CheckContext): Map<string, Type> {
    const substitutions = new Map<string, Type>();
    const names = new Set(callee.typeParameters.map((parameter) => parameter.name));
    callee.typeParameters.forEach((parameter, index) => {
      const argument = call.typeArguments[index];
      if (argument) substitutions.set(parameter.name, this.resolver.resolve(argument));
    });
    for (let index = 0; index < call.arguments.length; index++) {
      const argument = call.arguments[index]!;
      if (argument.kind === SyntaxKind.SpreadElement) continue;
      const parameter = callee.parameters[Math.min(index, callee.parameters.length - 1)];
      if (!parameter) continue;
      const argumentType = this.resolver.inferExpression(argument, (symbol) => this.lookup(symbol.id, flow));
      const pattern = parameter.rest && parameter.type.kind === "array" ? parameter.type.element : parameter.type;
      this.inferFromPattern(pattern, argumentType, substitutions, names);
    }
    return substitutions;
  },

  inferFromPattern(pattern: Type, actual: Type, substitutions: Map<string, Type>, names: ReadonlySet<string>): void {
    if (pattern.kind === "typeParam" && names.has(pattern.name) && !substitutions.has(pattern.name)) {
      substitutions.set(pattern.name, actual);
      return;
    }
    if (pattern.kind === "array" && actual.kind === "array") {
      this.inferFromPattern(pattern.element, actual.element, substitutions, names);
      return;
    }
    if (pattern.kind === "union") {
      for (const member of pattern.members) this.inferFromPattern(member, actual, substitutions, names);
    }
  },

  substitute(type: Type, substitutions: ReadonlyMap<string, Type>): Type {
    if (substitutions.size === 0) return type;
    switch (type.kind) {
      case "typeParam":
        return substitutions.get(type.name) ?? type;
      case "array":
        return { kind: "array", element: this.substitute(type.element, substitutions) };
      case "tuple":
        return { kind: "tuple", elements: type.elements.map((element) => this.substitute(element, substitutions)) };
      case "union":
        return unionOf(type.members.map((member) => this.substitute(member, substitutions)));
      case "object": {
        const properties = new Map<string, { type: Type; optional: boolean }>();
        for (const [name, property] of type.properties) properties.set(name, { type: this.substitute(property.type, substitutions), optional: property.optional });
        return { kind: "object", properties, ...(type.index ? { index: this.substitute(type.index, substitutions) } : {}), closed: type.closed, ...(type.label ? { label: type.label } : {}) };
      }
      case "function":
        return {
          kind: "function",
          parameters: type.parameters.map((parameter) => ({ ...parameter, type: this.substitute(parameter.type, substitutions) })),
          returnType: this.substitute(type.returnType, substitutions),
          typeParameters: type.typeParameters,
          ...(type.label ? { label: type.label } : {}),
        };
      default:
        return type;
    }
  },

  lookupProperty(base: Type, name: string): { type: Type; missing: boolean } {
    const members = unionMembers(base).filter(
      (member) => !(member.kind === "primitive" && (member.name === "null" || member.name === "undefined")),
    );
    if (members.length === 0) return { type: ANY, missing: false };
    const types: Type[] = [];
    let missing = false;
    for (const member of members) {
      const found = this.lookupIn(member, name);
      if (found === undefined) missing = true;
      else types.push(found);
    }
    if (missing) return { type: ANY, missing: true };
    return { type: unionOf(types), missing: false };
  },

  lookupIn(type: Type, name: string): Type | undefined {
    if (type.kind === "object") {
      const property = type.properties.get(name);
      if (property) return property.type;
      if (type.index) return type.index;
      if (OBJECT_PROTOTYPE_NAMES.has(name)) return ANY;
      return type.closed ? undefined : ANY;
    }
    // Arrays, tuples, functions and primitives have members this model does
    // not enumerate; a missing read there is never reported.
    if (type.kind === "array" || type.kind === "tuple" || type.kind === "function") return ANY;
    return ANY;
  },
};
