/**
 * Shared state types and the runtime declaration list for the LLVM generator.
 */

import type { FunctionInfo } from "../../binder/binder.js";
import type { BindResult } from "../../binder/binder.js";
import type { BuiltinFunction, ExtensionModule } from "../../extensions/registry.js";
import type { Block } from "../../ast/nodes.js";

export interface Slot {
  /** Register holding the `i64*` to the storage. */
  readonly ptr: string;
  readonly boxed: boolean;
}

/**
 * One enclosing `try`/`finally` region, used to run the `finally` block when an
 * abrupt completion (`return`/`break`/`continue`) exits it.
 */
export interface FinallyContext {
  /** Number of active `try` frames just before this region's frame. */
  readonly frameDepth: number;
  readonly block: Block;
}

export interface LoopLabels {
  readonly breakLabel: string;
  readonly continueLabel: string;
  /** Number of enclosing `try` frames when the loop was entered. */
  readonly tryDepth?: number;
  /** Labels attached to this loop (outermost first), for `break`/`continue`. */
  readonly labels?: readonly string[];
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
  /** Labels on enclosing `LabeledStatement`s, consumed by the next loop. */
  readonly pendingLabels: string[];
  readonly tryFrames: string[];
  readonly finallyStack: FinallyContext[];
  readonly escapePointers: string[];
  /** Alloca holding the `this` binding for the current function. */
  thisPtr?: string;
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
  /** Importable modules supplied by registered extensions. */
  readonly modules?: Readonly<Record<string, ExtensionModule>>;
  /**
   * Module specifier -> extension name for modules that a *known but
   * unregistered* extension provides. Used to report an actionable
   * `pass --ext node` diagnostic when such a module is imported without
   * enabling the extension.
   */
  readonly moduleHints?: Readonly<Record<string, string>>;
  /**
   * Host the IR is generated for. xbintsc builds for its own host, so this
   * defaults to the running process; it is overridable for tests.
   */
  readonly target?: { readonly platform: string; readonly arch: string };
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
  "declare i64 @xt_function_set_metadata(i64, i64, i32)",
  "declare i64 @xt_function_set_generator(i64)",
  "declare i64 @xt_closure_call(i64, i32, i64*)",
  "declare i64 @xt_closure_env(i64, i32)",
  "declare i64 @xt_call_with_this(i64, i64, i32, i64*)",
  "declare i64 @xt_this()",
  "declare i64 @xt_new(i64, i32, i64*)",
  "declare i64 @xt_instance_of(i64, i64)",
  "declare i64 @xt_function_set_prototype(i64, i64)",
  "declare i64 @xt_function_get_prototype(i64)",
  "declare i64 @xt_object_new_with_proto(i64)",
  "declare i64 @xt_object_get_prototype(i64)",
  "declare i64 @xt_object_set_prototype(i64, i64)",
  "declare i64 @xt_object_define_getter(i64, i64, i64)",
  "declare i64 @xt_object_define_setter(i64, i64, i64)",
  "declare i64 @xt_await(i64)",
  "declare void @xt_drain_microtasks()",
  "declare void @xt_run_event_loop()",
  "declare i64 @xt_promise_resolve(i64)",
  "declare i64 @xt_promise_reject(i64)",
  "declare i64 @xt_promise_ctor(i32, i64*)",
  "declare i64 @xt_promise_static(i64, i32, i64*)",
  "declare i64 @xt_array_static(i64, i32, i64*)",
  "declare i64 @xt_object_static(i64, i32, i64*)",
  "declare i64 @xt_number_static(i64, i32, i64*)",
  "declare i64 @xt_string_static(i64, i32, i64*)",
  "declare i64 @xt_date_static(i64, i32, i64*)",
  "declare i64 @xt_path_static(i64, i32, i64*)",
  "declare i64 @xt_os_static(i64, i32, i64*)",
  "declare i64 @xt_buffer_static(i64, i32, i64*)",
  "declare i64 @xt_buffer_ctor(i32, i64*)",
  "declare i64 @xt_stream_static(i64, i32, i64*)",
  "declare i64 @xt_readable_ctor(i32, i64*)",
  "declare i64 @xt_writable_ctor(i32, i64*)",
  "declare i64 @xt_duplex_ctor(i32, i64*)",
  "declare i64 @xt_transform_ctor(i32, i64*)",
  "declare i64 @xt_pass_through_ctor(i32, i64*)",
  "declare i64 @xt_net_static(i64, i32, i64*)",
  "declare i64 @xt_net_socket_ctor(i32, i64*)",
  "declare i64 @xt_net_server_ctor(i32, i64*)",
  "declare i64 @xt_dgram_static(i64, i32, i64*)",
  "declare i64 @xt_http_static(i64, i32, i64*)",
  "declare i64 @xt_event_emitter_ctor(i32, i64*)",
  "declare i64 @xt_events_static(i64, i32, i64*)",
  "declare i64 @xt_util_static(i64, i32, i64*)",
  "declare i64 @xt_querystring_static(i64, i32, i64*)",
  "declare i64 @xt_assert_static(i64, i32, i64*)",
  "declare i64 @xt_process_call(i64, i32, i64*)",
  "declare i64 @xt_process_get(i64)",
  "declare i64 @xt_map_ctor(i32, i64*)",
  "declare i64 @xt_set_ctor(i32, i64*)",
  "declare i64 @xt_date_ctor(i32, i64*)",
  "declare i64 @xt_regexp_ctor(i32, i64*)",
  "declare i64 @xt_error_ctor(i32, i64*)",
  "declare i64 @xt_type_error_ctor(i32, i64*)",
  "declare i64 @xt_range_error_ctor(i32, i64*)",
  "declare i64 @xt_syntax_error_ctor(i32, i64*)",
  "declare i64 @xt_reference_error_ctor(i32, i64*)",
  "declare i64 @xt_eval_error_ctor(i32, i64*)",
  "declare i64 @xt_uri_error_ctor(i32, i64*)",
  "declare i64 @xt_aggregate_error_ctor(i32, i64*)",
  "declare i64 @xt_error_constructor(i64)",
  "declare i64 @xt_error_prototype(i64)",
  "declare i64 @xt_error_init(i64, i64, i32, i64*)",
  "declare i64 @xt_json_parse(i32, i64*)",
  "declare i64 @xt_json_stringify(i32, i64*)",
  "declare i64 @xt_object_from_entries(i64)",
  "declare i64 @xt_object_freeze(i64)",
  "declare i32 @xt_object_is_frozen(i64)",
  "declare i64 @xt_object_has_own(i64, i64)",
  "declare i64 @xt_console_dir(i32, i64*)",
  "declare i64 @xt_console_trace(i32, i64*)",
  "declare i64 @xt_console_assert(i32, i64*)",
  "declare i64 @xt_console_count(i32, i64*)",
  "declare i64 @xt_console_count_reset(i32, i64*)",
  "declare i64 @xt_console_group(i32, i64*)",
  "declare i64 @xt_console_group_end(i32, i64*)",
  "declare i64 @xt_console_table(i32, i64*)",
  "declare i64 @xt_console_time(i32, i64*)",
  "declare i64 @xt_console_time_end(i32, i64*)",
  "declare i64 @xt_console_time_log(i32, i64*)",
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
  "declare i64 @xt_encode_uri_component(i32, i64*)",
  "declare i64 @xt_encode_uri(i32, i64*)",
  "declare i64 @xt_decode_uri_component(i32, i64*)",
  "declare i64 @xt_decode_uri(i32, i64*)",
  "declare i64 @xt_btoa(i32, i64*)",
  "declare i64 @xt_atob(i32, i64*)",
  "declare i64 @xt_number_ctor(i32, i64*)",
  "declare i64 @xt_bigint_ctor(i32, i64*)",
  "declare i64 @xt_bigint_static(i64, i32, i64*)",
  "declare i64 @xt_bigint_from_string(i8*, i64)",
  "declare i64 @xt_string_ctor(i32, i64*)",
  "declare i64 @xt_boolean_ctor(i32, i64*)",
  "declare i64 @xt_in(i64, i64)",
  "declare i64 @xt_delete(i64, i64)",
  "declare i64 @xt_rest_args(i32, i64*, i32)",
  "declare void @xt_set_program_args(i32, i8**)",
  "declare i64 @xt_import_meta(i64)",
  "declare i64 @xt_array_new(i32, i64*)",
  "declare i64 @xt_array_push(i64, i64)",
  "declare i64 @xt_array_length(i64)",
  "declare i64 @xt_array_spread(i64, i64)",
  "declare i64 @xt_iter_length(i64)",
  "declare i64 @xt_iter_open(i64)",
  "declare i64 @xt_iter_has(i64, i64)",
  "declare i64 @xt_iter_value(i64, i64)",
  "declare i64 @xt_yield(i64)",
  "declare i64 @xt_yield_star(i64)",
  "declare i32 @xt_array_size(i64)",
  "declare i64* @xt_array_items(i64)",
  "declare i64 @xt_builtin_value_parseInt(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_parseFloat(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_isNaN(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_isFinite(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_encodeURIComponent(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_encodeURI(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_decodeURIComponent(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_decodeURI(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_number(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_string(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_boolean(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_bigint(i64, i64, i32, i64*)",
  "declare i64 @xt_builtin_value_symbol(i64, i64, i32, i64*)",
  "declare i64 @xt_symbol(i32, i64*)",
  "declare i64 @xt_symbol_static(i64, i32, i64*)",
  "declare i64 @xt_symbol_get(i64)",
  "declare i64 @xt_get(i64, i64)",
  "declare i64 @xt_set(i64, i64, i64)",
  "declare i64 @xt_box_new(i64)",
  "declare i64 @xt_box_get(i64)",
  "declare i64 @xt_box_set(i64, i64)",
  "declare i64 @xt_is_nullish(i64)",
  "declare void @xt_throw(i64)",
  "declare i8* @llvm.frameaddress(i32)",
  "declare i32 @_setjmp(i8*, i8*) returns_twice",
  "declare i8* @xt_try_enter()",
  "declare i64 @xt_try_exception(i8*)",
  "declare void @xt_try_leave(i8*)",
  "declare void @xt_console_log(i32, i64*)",
  "declare void @xt_console_info(i32, i64*)",
  "declare void @xt_console_warn(i32, i64*)",
  "declare void @xt_console_error(i32, i64*)",
];
