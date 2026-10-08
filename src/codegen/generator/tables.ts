/**
 * Static tables and small pure helpers used by the LLVM generator.
 */

import {
  AssignmentOperator,
  BinaryOperator,
  SyntaxKind,
  type Node,
} from "../../ast/nodes.js";

/**
 * True when a `try` frame must be saved with `_setjmpex` rather than `_setjmp`.
 *
 * 64-bit Windows on ARM has no `_setjmp`: the UCRT declares `setjmp` as
 * `_setjmpex(buf, sponentry)` there, so the linker expects `_setjmpex` (with the
 * caller's stack pointer at entry, `llvm.sponentry`, instead of its frame
 * pointer). clang lowers a C `setjmp` on this target to exactly that pair.
 * Checked at code-generation time because xbintsc builds for its own host.
 */
export function requiresSetjmpex(platform: string, arch: string): boolean {
  return platform === "win32" && arch === "arm64";
}

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
  [BinaryOperator.InstanceOf]: "xt_instance_of",
};

export const CONSOLE_METHODS: Record<string, string> = {
  log: "xt_console_log",
  info: "xt_console_info",
  warn: "xt_console_warn",
  error: "xt_console_error",
  dir: "xt_console_dir",
  trace: "xt_console_trace",
  assert: "xt_console_assert",
  count: "xt_console_count",
  countReset: "xt_console_count_reset",
  group: "xt_console_group",
  groupEnd: "xt_console_group_end",
  table: "xt_console_table",
  time: "xt_console_time",
  timeEnd: "xt_console_time_end",
  timeLog: "xt_console_time_log",
};

export const MATH_FUNCTIONS = new Set<string>([
  "abs", "floor", "ceil", "round", "trunc", "sqrt", "cbrt", "pow", "exp", "log", "log2", "log10",
  "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "hypot", "sign", "random", "min", "max",
  "log1p", "expm1", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "fround", "imul", "clz32",
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

/** Read-only numeric constants exposed on the global `Number` namespace. */
export const NUMBER_CONSTANTS = new Set<string>([
  "MAX_SAFE_INTEGER",
  "MIN_SAFE_INTEGER",
  "MAX_VALUE",
  "MIN_VALUE",
  "EPSILON",
  "POSITIVE_INFINITY",
  "NEGATIVE_INFINITY",
  "NaN",
]);

/** The Error family. Each name is a first-class constructor: usable with `new`,
 * as a value (`instanceof`, `typeof`) and as a superclass (`extends TypeError`).
 * All map to runtime constructors with signature `(i32, i64*)`. */
export const ERROR_CONSTRUCTORS: Record<string, string> = {
  Error: "xt_error_ctor",
  TypeError: "xt_type_error_ctor",
  RangeError: "xt_range_error_ctor",
  SyntaxError: "xt_syntax_error_ctor",
  ReferenceError: "xt_reference_error_ctor",
  EvalError: "xt_eval_error_ctor",
  URIError: "xt_uri_error_ctor",
  AggregateError: "xt_aggregate_error_ctor",
};

export function isErrorFamily(name: string): boolean {
  return ERROR_CONSTRUCTORS[name] !== undefined;
}

export const GLOBAL_FUNCTIONS: Record<string, string> = {
  parseInt: "xt_parse_int",
  parseFloat: "xt_parse_float",
  isNaN: "xt_is_nan",
  isFinite: "xt_is_finite",
  encodeURIComponent: "xt_encode_uri_component",
  encodeURI: "xt_encode_uri",
  decodeURIComponent: "xt_decode_uri_component",
  decodeURI: "xt_decode_uri",
  btoa: "xt_btoa",
  atob: "xt_atob",
  setTimeout: "xt_set_timeout",
  clearTimeout: "xt_clear_timeout",
  setInterval: "xt_set_interval",
  clearInterval: "xt_clear_interval",
  fetch: "xt_fetch",
  Number: "xt_number_ctor",
  String: "xt_string_ctor",
  Boolean: "xt_boolean_ctor",
  BigInt: "xt_bigint_ctor",
  Symbol: "xt_symbol",
  ...ERROR_CONSTRUCTORS,
};

/** Global functions that are also usable as first-class values, each mapped to
 * a runtime trampoline with the closure ABI. */
export const BUILTIN_FUNCTION_VALUES: Record<string, string> = {
  parseInt: "xt_builtin_value_parseInt",
  parseFloat: "xt_builtin_value_parseFloat",
  isNaN: "xt_builtin_value_isNaN",
  isFinite: "xt_builtin_value_isFinite",
  encodeURIComponent: "xt_builtin_value_encodeURIComponent",
  encodeURI: "xt_builtin_value_encodeURI",
  decodeURIComponent: "xt_builtin_value_decodeURIComponent",
  decodeURI: "xt_builtin_value_decodeURI",
  setTimeout: "xt_builtin_value_setTimeout",
  clearTimeout: "xt_builtin_value_clearTimeout",
  setInterval: "xt_builtin_value_setInterval",
  clearInterval: "xt_builtin_value_clearInterval",
  fetch: "xt_builtin_value_fetch",
  Number: "xt_builtin_value_number",
  String: "xt_builtin_value_string",
  Boolean: "xt_builtin_value_boolean",
  BigInt: "xt_builtin_value_bigint",
  Symbol: "xt_builtin_value_symbol",
};

/** Typed-array globals: constructor, static dispatcher and element width. */
export const TYPED_ARRAYS: Record<string, { ctor: string; static: string; bytes: number }> = {
  Uint8Array: { ctor: "xt_ta_u8_ctor", static: "xt_ta_u8_static", bytes: 1 },
  Int8Array: { ctor: "xt_ta_i8_ctor", static: "xt_ta_i8_static", bytes: 1 },
  Uint8ClampedArray: { ctor: "xt_ta_u8c_ctor", static: "xt_ta_u8c_static", bytes: 1 },
  Uint16Array: { ctor: "xt_ta_u16_ctor", static: "xt_ta_u16_static", bytes: 2 },
  Int16Array: { ctor: "xt_ta_i16_ctor", static: "xt_ta_i16_static", bytes: 2 },
  Uint32Array: { ctor: "xt_ta_u32_ctor", static: "xt_ta_u32_static", bytes: 4 },
  Int32Array: { ctor: "xt_ta_i32_ctor", static: "xt_ta_i32_static", bytes: 4 },
  Float32Array: { ctor: "xt_ta_f32_ctor", static: "xt_ta_f32_static", bytes: 4 },
  Float64Array: { ctor: "xt_ta_f64_ctor", static: "xt_ta_f64_static", bytes: 8 },
};

const TYPED_ARRAY_CTORS: Record<string, string> = {
  Uint8Array: "xt_ta_u8_ctor",
  Int8Array: "xt_ta_i8_ctor",
  Uint8ClampedArray: "xt_ta_u8c_ctor",
  Uint16Array: "xt_ta_u16_ctor",
  Int16Array: "xt_ta_i16_ctor",
  Uint32Array: "xt_ta_u32_ctor",
  Int32Array: "xt_ta_i32_ctor",
  Float32Array: "xt_ta_f32_ctor",
  Float64Array: "xt_ta_f64_ctor",
};
const TYPED_ARRAY_STATICS: Record<string, string> = {
  Uint8Array: "xt_ta_u8_static",
  Int8Array: "xt_ta_i8_static",
  Uint8ClampedArray: "xt_ta_u8c_static",
  Uint16Array: "xt_ta_u16_static",
  Int16Array: "xt_ta_i16_static",
  Uint32Array: "xt_ta_u32_static",
  Int32Array: "xt_ta_i32_static",
  Float32Array: "xt_ta_f32_static",
  Float64Array: "xt_ta_f64_static",
};

/** Global namespaces whose static methods map to runtime dispatchers. */
export const NAMESPACE_STATICS: Record<string, string> = {
  Math: "xt_math_call",
  JSON: "xt_json",
  Array: "xt_array_static",
  Object: "xt_object_static",
  Number: "xt_number_static",
  String: "xt_string_static",
  BigInt: "xt_bigint_static",
  Symbol: "xt_symbol_static",
  Date: "xt_date_static",
  Promise: "xt_promise_static",
  path: "xt_path_static",
  os: "xt_os_static",
  process: "xt_process_call",
  Buffer: "xt_buffer_static",
  stream: "xt_stream_static",
  Readable: "xt_stream_static",
  Writable: "xt_stream_static",
  Duplex: "xt_stream_static",
  Transform: "xt_stream_static",
  PassThrough: "xt_stream_static",
  net: "xt_net_static",
  dgram: "xt_dgram_static",
  http: "xt_http_static",
  events: "xt_events_static",
  util: "xt_util_static",
  querystring: "xt_querystring_static",
  assert: "xt_assert_static",
  ...TYPED_ARRAY_STATICS,
};

/** Namespace identifiers whose property access maps to a runtime getter. */
export const NAMESPACE_PROPERTIES: Record<string, string> = {
  process: "xt_process_get",
  "import.meta": "xt_import_meta",
  Symbol: "xt_symbol_get",
};

/** Global constructors called as `new X(...)` (all have signature `(i32, i64*)`). */
export const CTOR_FUNCTIONS: Record<string, string> = {
  Array: "xt_array_new",
  Map: "xt_map_ctor",
  Set: "xt_set_ctor",
  Date: "xt_date_ctor",
  RegExp: "xt_regexp_ctor",
  Promise: "xt_promise_ctor",
  Buffer: "xt_buffer_ctor",
  Readable: "xt_readable_ctor",
  Writable: "xt_writable_ctor",
  Duplex: "xt_duplex_ctor",
  Transform: "xt_transform_ctor",
  PassThrough: "xt_pass_through_ctor",
  Socket: "xt_net_socket_ctor",
  Server: "xt_net_server_ctor",
  EventEmitter: "xt_event_emitter_ctor",
  ...ERROR_CONSTRUCTORS,
  ...TYPED_ARRAY_CTORS,
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

/**
 * The UTF-8 bytes of one code point, which must be an ASCII byte or a Unicode
 * scalar (`0..0x10ffff`, not a surrogate). Encoded by hand rather than with
 * `String.fromCharCode`/`String.fromCodePoint`, which a self-hosted build would
 * run through the runtime and encode a second time.
 */
export function codePointBytes(code: number): number[] {
  if (code < 0x80) return [code];
  if (code < 0x800) return [0xc0 | (code >> 6), 0x80 | (code & 0x3f)];
  if (code < 0x10000) return [0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f)];
  return [
    0xf0 | (code >> 18),
    0x80 | ((code >> 12) & 0x3f),
    0x80 | ((code >> 6) & 0x3f),
    0x80 | (code & 0x3f),
  ];
}

/**
 * The bytes a string literal stands for.
 *
 * A compiled string is a sequence of UTF-8 bytes (`"é".length` is 1 and
 * `"😀".length` is 4 - see doc/ai/language-support.md). The text reaching this
 * point is whatever a host's strings are, and both shapes have to produce the
 * same bytes:
 *
 * - a UTF-16 host decodes a file into characters, so `é` is one code unit
 *   (0xe9) and has to be encoded here;
 * - the self-hosted runtime has no UTF-8 decoder, so `readFileSync` hands over
 *   a file's bytes, and `é` is already `c3 a9`. Encoding that again is what
 *   turned the three bytes of `—` into six and broke the self-hosting fixpoint.
 *
 * A code unit above 0xff can only be text, so its presence settles which shape
 * this is: text is encoded (once), and text that is already a byte sequence is
 * emitted as it is. A source file read on either host therefore lands on the
 * same bytes, which is what the fixpoint needs.
 */
export function utf8Bytes(text: string): number[] {
  if (!hasMultiByteUnit(text)) {
    const bytes: number[] = [];
    for (let index = 0; index < text.length; index++) bytes.push(text.charCodeAt(index) & 0xff);
    return bytes;
  }
  const bytes: number[] = [];
  const length = text.length;
  for (let index = 0; index < length; index++) {
    let code = text.charCodeAt(index);
    if (code >= 0xd800 && code <= 0xdbff && index + 1 < length) {
      const low = text.charCodeAt(index + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        index++;
      }
    }
    for (const byte of codePointBytes(code)) bytes.push(byte);
  }
  return bytes;
}

/** Whether any code unit is above one byte, which means text, not bytes. */
function hasMultiByteUnit(text: string): boolean {
  for (let index = 0; index < text.length; index++) {
    if (text.charCodeAt(index) > 0xff) return true;
  }
  return false;
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
