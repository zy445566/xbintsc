/**
 * Shared state types and the runtime declaration list for the LLVM generator.
 */

import type { FunctionInfo } from "../../binder/binder.js";
import type { BindResult } from "../../binder/binder.js";
import type { BuiltinFunction } from "../../extensions/registry.js";

export interface Slot {
  /** Register holding the `i64*` to the storage. */
  readonly ptr: string;
  readonly boxed: boolean;
}

export interface LoopLabels {
  readonly breakLabel: string;
  readonly continueLabel: string;
  /** Number of enclosing `try` frames when the loop was entered. */
  readonly tryDepth?: number;
}

export interface FunctionState {
  readonly fn: FunctionInfo;
  readonly buffer: string[];
  readonly allocas: string[];
  readonly slots: Map<number, Slot>;
  reg: number;
  label: number;
  terminated: boolean;
  readonly loops: LoopLabels[];
  readonly tryFrames: string[];
  readonly escapePointers: string[];
  /** Set when the function contains a `try`, forcing locals to live in memory. */
  usesTry: boolean;
}

export interface CodegenResult {
  readonly ir: string;
  readonly binding: BindResult;
}

export interface CodegenOptions {
  /** Global names supplied by registered extensions. */
  readonly builtins?: Readonly<Record<string, BuiltinFunction>>;
}

export const RUNTIME_DECLARATIONS: readonly string[] = [
  "declare i64 @xt_undefined()",
  "declare i64 @xt_null()",
  "declare i64 @xt_bool(i32)",
  "declare i64 @xt_number(double)",
  "declare i64 @xt_string_new(i8*, i64)",
  "declare i64 @xt_string_from_cstr(i8*)",
  "declare i64 @xt_arg(i32, i64*, i32)",
  "declare i64 @xt_closure_new(i8*, i32, i64*)",
  "declare i64 @xt_closure_call(i64, i32, i64*)",
  "declare i64 @xt_closure_env(i64, i32)",
  "declare i32 @xt_truthy(i64)",
  "declare double @xt_to_number(i64)",
  "declare i64 @xt_to_string(i64)",
  "declare i64 @xt_typeof(i64)",
  "declare i64 @xt_add(i64, i64)",
  "declare i64 @xt_sub(i64, i64)",
  "declare i64 @xt_mul(i64, i64)",
  "declare i64 @xt_div(i64, i64)",
  "declare i64 @xt_mod(i64, i64)",
  "declare i64 @xt_pow(i64, i64)",
  "declare i64 @xt_neg(i64)",
  "declare i64 @xt_pos(i64)",
  "declare i64 @xt_bit_and(i64, i64)",
  "declare i64 @xt_bit_or(i64, i64)",
  "declare i64 @xt_bit_xor(i64, i64)",
  "declare i64 @xt_bit_not(i64)",
  "declare i64 @xt_shl(i64, i64)",
  "declare i64 @xt_shr(i64, i64)",
  "declare i64 @xt_ushr(i64, i64)",
  "declare i64 @xt_lt(i64, i64)",
  "declare i64 @xt_le(i64, i64)",
  "declare i64 @xt_gt(i64, i64)",
  "declare i64 @xt_ge(i64, i64)",
  "declare i64 @xt_eq(i64, i64)",
  "declare i64 @xt_ne(i64, i64)",
  "declare i64 @xt_seq(i64, i64)",
  "declare i64 @xt_sne(i64, i64)",
  "declare i64 @xt_not(i64)",
  "declare i64 @xt_object_new()",
  "declare i64 @xt_object_get_cstr(i64, i8*)",
  "declare i64 @xt_object_set(i64, i64, i64)",
  "declare i64 @xt_object_has(i64, i64)",
  "declare i64 @xt_object_keys(i64)",
  "declare i64 @xt_object_values(i64)",
  "declare i64 @xt_object_entries(i64)",
  "declare i64 @xt_object_assign(i32, i64*)",
  "declare i64 @xt_object_spread(i64, i64)",
  "declare i64 @xt_call_method(i64, i64, i32, i64*)",
  "declare i64 @xt_math_call(i64, i32, i64*)",
  "declare i64 @xt_parse_int(i32, i64*)",
  "declare i64 @xt_parse_float(i32, i64*)",
  "declare i64 @xt_is_nan(i32, i64*)",
  "declare i64 @xt_is_finite(i32, i64*)",
  "declare i64 @xt_number_ctor(i32, i64*)",
  "declare i64 @xt_string_ctor(i32, i64*)",
  "declare i64 @xt_boolean_ctor(i32, i64*)",
  "declare i64 @xt_in(i64, i64)",
  "declare i64 @xt_delete(i64, i64)",
  "declare i64 @xt_rest_args(i32, i64*, i32)",
  "declare i64 @xt_array_new(i32, i64*)",
  "declare i64 @xt_array_push(i64, i64)",
  "declare i64 @xt_array_length(i64)",
  "declare i64 @xt_array_spread(i64, i64)",
  "declare i64 @xt_get(i64, i64)",
  "declare i64 @xt_set(i64, i64, i64)",
  "declare i64 @xt_box_new(i64)",
  "declare i64 @xt_box_get(i64)",
  "declare i64 @xt_box_set(i64, i64)",
  "declare i64 @xt_is_nullish(i64)",
  "declare void @xt_throw(i64)",
  "declare i32 @_setjmp(i8*) returns_twice",
  "declare i8* @xt_try_enter()",
  "declare i64 @xt_try_exception(i8*)",
  "declare void @xt_try_leave(i8*)",
  "declare void @xt_console_log(i32, i64*)",
  "declare void @xt_console_info(i32, i64*)",
  "declare void @xt_console_warn(i32, i64*)",
  "declare void @xt_console_error(i32, i64*)",
];
