/**
 * Structural assignability for the small type model.
 *
 * This is intentionally lenient: every case the model cannot decide returns
 * `true`. A `false` is reserved for a definite mismatch (a string where a
 * number is required, a required property that is absent from a closed shape),
 * because a false negative only hides a bug while a false positive rejects
 * working code.
 */

import { isAny, unionMembers, type Type } from "./type-model.js";

const MAX_DEPTH = 8;

export function isAssignable(source: Type, target: Type, depth = 0): boolean {
  if (depth > MAX_DEPTH) return true;
  if (isAny(source) || isAny(target)) return true;

  // Every member of a source union must fit; a target union accepts if any
  // member fits. Source first, so `("a" | "b")` fits `("a" | "b")`.
  if (source.kind === "union") {
    return source.members.every((member) => isAssignable(member, target, depth + 1));
  }
  if (target.kind === "union") {
    return target.members.some((member) => isAssignable(source, member, depth + 1));
  }

  if (target.kind === "literal") {
    if (source.kind === "literal") return source.value === target.value;
    if (source.kind === "primitive") return source.name === target.primitive;
    return false;
  }

  if (target.kind === "primitive") {
    return assignableToPrimitive(source, target.name);
  }

  if (source.kind === "literal") {
    if (target.kind === "object") return false;
    if (target.kind === "array" || target.kind === "tuple" || target.kind === "function") return false;
    return true;
  }

  if (source.kind === "primitive") {
    // `null` / `undefined` are accepted everywhere: the compiler does not
    // enable strict-null checking, so rejecting them would break working code.
    if (source.name === "null" || source.name === "undefined" || source.name === "void") return true;
    if (target.kind === "object") return false;
    if (target.kind === "array" || target.kind === "tuple" || target.kind === "function") return false;
    return true;
  }

  if (target.kind === "array") {
    if (source.kind === "array") return isAssignable(source.element, target.element, depth + 1);
    if (source.kind === "tuple") return source.elements.every((element) => isAssignable(element, target.element, depth + 1));
    return false;
  }

  if (target.kind === "tuple") {
    if (source.kind === "tuple") {
      if (source.elements.length !== target.elements.length) return false;
      return source.elements.every((element, index) => isAssignable(element, target.elements[index]!, depth + 1));
    }
    // Array-to-tuple is not generally safe, but the model cannot prove it is
    // wrong, so stay silent.
    return source.kind === "array";
  }

  if (target.kind === "object") {
    if (source.kind === "array" || source.kind === "tuple") {
      // Arrays have prototype members this model does not enumerate; only a
      // target with no required properties can be judged.
      return [...target.properties.values()].every((property) => property.optional);
    }
    if (source.kind !== "object") return false;
    for (const [name, targetProperty] of target.properties) {
      const sourceProperty = source.properties.get(name);
      if (!sourceProperty) {
        if (targetProperty.optional) continue;
        if (source.index && isAssignable(source.index, targetProperty.type, depth + 1)) continue;
        if (!target.closed) continue;
        return false;
      }
      if (!isAssignable(sourceProperty.type, targetProperty.type, depth + 1)) return false;
    }
    if (target.index) {
      if (source.index && !isAssignable(source.index, target.index, depth + 1)) return false;
      for (const property of source.properties.values()) {
        if (!isAssignable(property.type, target.index, depth + 1)) return false;
      }
    }
    return true;
  }

  if (target.kind === "function") {
    if (source.kind !== "function") {
      // A function-typed value can also wear object members; do not guess.
      return source.kind === "object";
    }
    const required = target.parameters.filter((parameter) => !parameter.optional && !parameter.rest).length;
    if (source.parameters.length < required) return false;
    for (let index = 0; index < target.parameters.length; index++) {
      const sourceParameter = source.parameters[index];
      const targetParameter = target.parameters[index];
      if (!sourceParameter || !targetParameter) break;
      if (!isAssignable(targetParameter.type, sourceParameter.type, depth + 1)) return false;
    }
    return isAssignable(source.returnType, target.returnType, depth + 1);
  }

  return true;
}

function assignableToPrimitive(source: Type, name: string): boolean {
  if (name === "null" || name === "undefined" || name === "void") return true;
  if (source.kind === "literal") return source.primitive === name;
  if (source.kind === "primitive") {
    if (source.name === "null" || source.name === "undefined" || source.name === "void") return true;
    return source.name === name;
  }
  if (name === "object") return source.kind === "object" || source.kind === "array" || source.kind === "tuple" || source.kind === "function";
  return false;
}

/** True only when the value is definitely not callable (not merely unknown). */
export function isDefinitelyNotCallable(type: Type): boolean {
  const members = unionMembers(type);
  return members.every((member) => {
    if (member.kind === "primitive") {
      return member.name === "number" || member.name === "string" || member.name === "boolean" || member.name === "bigint" || member.name === "symbol" || member.name === "null" || member.name === "undefined";
    }
    return member.kind === "literal";
  });
}
