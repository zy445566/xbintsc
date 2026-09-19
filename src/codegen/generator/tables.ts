/**
 * Static tables and small pure helpers used by the LLVM generator.
 */

import {
  AssignmentOperator,
  BinaryOperator,
  SyntaxKind,
  type Node,
} from "../../ast/nodes.js";

export const BINARY_RUNTIME: Record<string, string | undefined> = {
  [BinaryOperator.Add]: "xt_add",
  [BinaryOperator.Subtract]: "xt_sub",
  [BinaryOperator.Multiply]: "xt_mul",
  [BinaryOperator.Divide]: "xt_div",
  [BinaryOperator.Remainder]: "xt_mod",
  [BinaryOperator.Exponent]: "xt_pow",
  [BinaryOperator.LessThan]: "xt_lt",
  [BinaryOperator.LessThanEquals]: "xt_le",
  [BinaryOperator.GreaterThan]: "xt_gt",
  [BinaryOperator.GreaterThanEquals]: "xt_ge",
  [BinaryOperator.EqualsEquals]: "xt_eq",
  [BinaryOperator.ExclamationEquals]: "xt_ne",
  [BinaryOperator.EqualsEqualsEquals]: "xt_seq",
  [BinaryOperator.ExclamationEqualsEquals]: "xt_sne",
  [BinaryOperator.Ampersand]: "xt_bit_and",
  [BinaryOperator.Bar]: "xt_bit_or",
  [BinaryOperator.Caret]: "xt_bit_xor",
  [BinaryOperator.LessThanLessThan]: "xt_shl",
  [BinaryOperator.GreaterThanGreaterThan]: "xt_shr",
  [BinaryOperator.GreaterThanGreaterThanGreaterThan]: "xt_ushr",
  [BinaryOperator.In]: "xt_in",
};

export const CONSOLE_METHODS: Record<string, string> = {
  log: "xt_console_log",
  info: "xt_console_info",
  warn: "xt_console_warn",
  error: "xt_console_error",
};

export const MATH_FUNCTIONS = new Set<string>([
  "abs", "floor", "ceil", "round", "trunc", "sqrt", "cbrt", "pow", "exp", "log", "log2", "log10",
  "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "hypot", "sign", "random", "min", "max",
]);

export const MATH_CONSTANTS: Record<string, number> = {
  PI: Math.PI,
  E: Math.E,
  LN2: Math.LN2,
  LN10: Math.LN10,
  LOG2E: Math.LOG2E,
  LOG10E: Math.LOG10E,
  SQRT2: Math.SQRT2,
  SQRT1_2: Math.SQRT1_2,
};

export const GLOBAL_FUNCTIONS: Record<string, string> = {
  parseInt: "xt_parse_int",
  parseFloat: "xt_parse_float",
  isNaN: "xt_is_nan",
  isFinite: "xt_is_finite",
  Number: "xt_number_ctor",
  String: "xt_string_ctor",
  Boolean: "xt_boolean_ctor",
};

export const BUILTIN_METHODS = new Set<string>([
  "push", "pop", "shift", "unshift", "join", "slice", "indexOf", "includes", "map", "forEach", "filter",
  "reduce", "concat", "reverse", "charAt", "charCodeAt", "substring", "substr", "split", "toUpperCase",
  "toLowerCase", "trim", "replace", "repeat", "startsWith", "endsWith",
]);

export const ASSIGNMENT_OPERATORS = new Set<string>([
  "=",
  "+=",
  "-=",
  "*=",
  "/=",
  "%=",
  "**=",
  "<<=",
  ">>=",
  ">>>=",
  "&=",
  "|=",
  "^=",
  "&&=",
  "||=",
  "??=",
]);

export function isAssignmentOperator(operator: string): boolean {
  return ASSIGNMENT_OPERATORS.has(operator);
}

export function compoundToBinary(operator: AssignmentOperator): BinaryOperator {
  const text = operator.slice(0, -1);
  return text as BinaryOperator;
}

export function propertyNameText(name: Node): string {
  switch (name.kind) {
    case SyntaxKind.Identifier:
    case SyntaxKind.PrivateIdentifier:
      return (name as unknown as { text: string }).text;
    case SyntaxKind.StringLiteral:
      return (name as unknown as { value: string }).value;
    case SyntaxKind.NumericLiteral:
      return String((name as unknown as { value: number }).value);
    default:
      return "";
  }
}

/** Human readable syntax kind for diagnostics (const enums have no reverse map). */
export function kindName(kind: number): string {
  const entry = KIND_NAMES.get(kind);
  return entry ?? String(kind);
}

export const KIND_NAMES = new Map<number, string>([
  [SyntaxKind.CallExpression, "call expression"],
  [SyntaxKind.NewExpression, "new expression"],
  [SyntaxKind.ClassDeclaration, "class declaration"],
  [SyntaxKind.ClassExpression, "class expression"],
  [SyntaxKind.EnumDeclaration, "enum declaration"],
  [SyntaxKind.SwitchStatement, "switch statement"],
  [SyntaxKind.TryStatement, "try statement"],
  [SyntaxKind.SpreadElement, "spread element"],
  [SyntaxKind.ThisKeyword, "this expression"],
  [SyntaxKind.RegularExpressionLiteral, "regular expression"],
  [SyntaxKind.TaggedTemplateExpression, "tagged template"],
  [SyntaxKind.DeleteExpression, "delete expression"],
  [SyntaxKind.AwaitExpression, "await expression"],
  [SyntaxKind.YieldExpression, "yield expression"],
  [SyntaxKind.SwitchStatement, "switch statement"],
]);

export function utf8Bytes(text: string): number[] {
  const bytes: number[] = [];
  for (const character of text) {
    const code = character.codePointAt(0)!;
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) {
      bytes.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
      bytes.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    } else {
      bytes.push(
        0xf0 | (code >> 18),
        0x80 | ((code >> 12) & 0x3f),
        0x80 | ((code >> 6) & 0x3f),
        0x80 | (code & 0x3f),
      );
    }
  }
  return bytes;
}

export function escapeBytes(bytes: readonly number[]): string {
  return bytes
    .map((byte) => {
      if (byte === 0x22) return '\\22';
      if (byte === 0x5c) return '\\5C';
      if (byte >= 0x20 && byte < 0x7f) return String.fromCharCode(byte);
      return `\\${byte.toString(16).toUpperCase().padStart(2, "0")}`;
    })
    .join("");
}
