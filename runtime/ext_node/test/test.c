/*
 * Node.js `node:test` module for xbintsc.
 *
 * A compact, synchronous TAP runner. `test(name, fn)` runs the callback
 * immediately, catching a thrown `AssertionError` (or any error) and emitting
 * `ok` / `not ok` lines. A summary is printed once, at process exit, via
 * `atexit`, and a failed run exits with status 1 like Node's runner.
 *
 * `import test from "node:test"` binds the callable default export
 * (`xt_node_test`); `test.skip`, `test.todo` and the hook helpers are provided
 * as no-op/wrapper variants.
 */

#include "../node_common.h"

#include <string.h>

static int xt_test_count = 0;
static int xt_test_pass = 0;
static int xt_test_fail = 0;
static int xt_test_started = 0;
static int xt_test_reported = 0;

static void xt_test_report(void) {
  if (xt_test_reported) return;
  xt_test_reported = 1;
  if (!xt_test_started) return;
  fflush(stdout);
  printf("1..%d\n", xt_test_count);
  printf("# tests %d\n", xt_test_count);
  printf("# pass %d\n", xt_test_pass);
  if (xt_test_fail > 0) printf("# fail %d\n", xt_test_fail);
  fflush(stdout);
  if (xt_test_fail > 0) exit(1);
}

static void xt_test_begin(void) {
  if (xt_test_started) return;
  xt_test_started = 1;
  printf("TAP version 13\n");
  atexit(xt_test_report);
}

static const char *xt_test_name(int32_t argc, xt_value *argv) {
  xt_value name = xt_arg(argc, argv, 0);
  const char *text = name == XT_UNDEFINED ? NULL : xt_string_data(xt_to_string(name));
  return text ? text : "<anonymous>";
}

xt_value xt_node_test(int32_t argc, xt_value *argv) {
  xt_test_begin();
  const char *name = xt_test_name(argc, argv);
  xt_value fn = xt_arg(argc, argv, 1);
  int index = ++xt_test_count;
  printf("# Subtest: %s\n", name);
  if (!XT_IS_FUNCTION(fn)) {
    xt_test_pass += 1;
    printf("ok %d - %s\n", index, name);
    return xt_undefined();
  }
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    xt_closure_call(fn, 0, NULL);
    xt_try_leave(frame);
    xt_test_pass += 1;
    printf("ok %d - %s\n", index, name);
  } else {
    xt_value error = xt_try_exception(frame);
    xt_try_leave(frame);
    xt_test_fail += 1;
    printf("not ok %d - %s\n", index, name);
    xt_value text = XT_IS_OBJECT(error) ? xt_error_to_string(error) : xt_to_string(error);
    const char *message = xt_string_data(text);
    printf("  ---\n  error: '%s'\n  ...\n", message ? message : "unknown error");
  }
  return xt_undefined();
}

xt_value xt_node_test_skip(int32_t argc, xt_value *argv) {
  xt_test_begin();
  const char *name = xt_test_name(argc, argv);
  int index = ++xt_test_count;
  xt_test_pass += 1;
  printf("ok %d - %s # SKIP\n", index, name);
  return xt_undefined();
}

xt_value xt_node_test_todo(int32_t argc, xt_value *argv) {
  xt_test_begin();
  const char *name = xt_test_name(argc, argv);
  int index = ++xt_test_count;
  xt_test_pass += 1;
  printf("ok %d - %s # TODO\n", index, name);
  return xt_undefined();
}

/* `before`/`after`/`beforeEach`/`afterEach` are accepted and ignored. */
xt_value xt_node_test_hook(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_undefined();
}
