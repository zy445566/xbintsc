/*
 * Node.js `console` module for xbintsc.
 *
 * `new Console({ stdout, stderr })` returns an object whose methods route to the
 * global `xt_console_*` functions (the stdout/stderr options are ignored). The
 * module also exposes the same methods as named exports.
 */

#include "../node_common.h"

/* A method whose runtime function returns void. */
#define XT_CONSOLE_VOID(methodName, target)                                             \
  static xt_value methodName(xt_value thisValue, xt_value env, int32_t argc,            \
                             xt_value *argv) {                                          \
    (void)thisValue;                                                                    \
    (void)env;                                                                          \
    target(argc, argv);                                                                 \
    return XT_UNDEFINED;                                                                \
  }

/* A method whose runtime function returns a value. */
#define XT_CONSOLE_VALUE(methodName, target)                                            \
  static xt_value methodName(xt_value thisValue, xt_value env, int32_t argc,            \
                             xt_value *argv) {                                          \
    (void)thisValue;                                                                    \
    (void)env;                                                                          \
    return target(argc, argv);                                                          \
  }

XT_CONSOLE_VOID(console_log_method, xt_console_log)
XT_CONSOLE_VOID(console_info_method, xt_console_info)
XT_CONSOLE_VOID(console_warn_method, xt_console_warn)
XT_CONSOLE_VOID(console_error_method, xt_console_error)
XT_CONSOLE_VALUE(console_dir_method, xt_console_dir)
XT_CONSOLE_VALUE(console_trace_method, xt_console_trace)
XT_CONSOLE_VALUE(console_assert_method, xt_console_assert)
XT_CONSOLE_VALUE(console_count_method, xt_console_count)
XT_CONSOLE_VALUE(console_count_reset_method, xt_console_count_reset)
XT_CONSOLE_VALUE(console_group_method, xt_console_group)
XT_CONSOLE_VALUE(console_group_end_method, xt_console_group_end)
XT_CONSOLE_VALUE(console_table_method, xt_console_table)
XT_CONSOLE_VALUE(console_time_method, xt_console_time)
XT_CONSOLE_VALUE(console_time_end_method, xt_console_time_end)
XT_CONSOLE_VALUE(console_time_log_method, xt_console_time_log)

xt_value xt_console_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value object = xt_object_new();
  xt_node_set(object, "log", xt_closure_new((void *)console_log_method, 0, NULL));
  xt_node_set(object, "info", xt_closure_new((void *)console_info_method, 0, NULL));
  xt_node_set(object, "warn", xt_closure_new((void *)console_warn_method, 0, NULL));
  xt_node_set(object, "error", xt_closure_new((void *)console_error_method, 0, NULL));
  xt_node_set(object, "debug", xt_closure_new((void *)console_log_method, 0, NULL));
  xt_node_set(object, "dir", xt_closure_new((void *)console_dir_method, 0, NULL));
  xt_node_set(object, "trace", xt_closure_new((void *)console_trace_method, 0, NULL));
  xt_node_set(object, "assert", xt_closure_new((void *)console_assert_method, 0, NULL));
  xt_node_set(object, "count", xt_closure_new((void *)console_count_method, 0, NULL));
  xt_node_set(object, "countReset", xt_closure_new((void *)console_count_reset_method, 0, NULL));
  xt_node_set(object, "group", xt_closure_new((void *)console_group_method, 0, NULL));
  xt_node_set(object, "groupEnd", xt_closure_new((void *)console_group_end_method, 0, NULL));
  xt_node_set(object, "table", xt_closure_new((void *)console_table_method, 0, NULL));
  xt_node_set(object, "time", xt_closure_new((void *)console_time_method, 0, NULL));
  xt_node_set(object, "timeEnd", xt_closure_new((void *)console_time_end_method, 0, NULL));
  xt_node_set(object, "timeLog", xt_closure_new((void *)console_time_log_method, 0, NULL));
  return object;
}
