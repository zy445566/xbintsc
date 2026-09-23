/*
 * Node.js `assert` module for xbintsc.
 *
 * Implements the commonly used assertion surface on top of the runtime's
 * exception machinery: a failed assertion throws an `AssertionError`, exactly
 * like Node. `node:test` catches that error and reports the test as failed.
 *
 * All entry points share the namespace dispatcher `xt_assert_static`, so both
 * `import assert from "node:assert"` (`assert.strictEqual(...)`) and named
 * imports (`import { strictEqual } from "assert"`) work.
 */

#include "../node_common.h"

#include <string.h>

/* An `AssertionError` is an ordinary Error-family object; set the error kind so
 * `xt_to_string`/inspection formats it as `AssertionError: message`. */
static void assert_throw(xt_value actual, xt_value expected, xt_value message) {
  xt_value error = xt_object_new();
  ((xt_object *)XT_GET_PTR(error))->header.kind = XT_OBJECT_KIND_ERROR;
  xt_node_set(error, "name", xt_string_from_cstr("AssertionError"));
  xt_node_set(error, "message", message);
  xt_node_set(error, "code", xt_string_from_cstr("ERR_ASSERTION"));
  if (!xt_truthy(xt_is_nullish(actual))) xt_node_set(error, "actual", actual);
  if (!xt_truthy(xt_is_nullish(expected))) xt_node_set(error, "expected", expected);
  xt_throw(error);
}

/* `message` is the user supplied message (or undefined); otherwise `fallback`
 * is used. */
static xt_value assert_message(xt_value message, const char *fallback) {
  if (!xt_truthy(xt_is_nullish(message))) return xt_to_string(message);
  return xt_string_from_cstr(fallback);
}

static xt_value assert_join3(xt_value a, const char *middle, xt_value b) {
  xt_value left = xt_add(a, xt_string_from_cstr(middle));
  return xt_add(left, xt_to_string(b));
}

/* -- deep equality -------------------------------------------------------- */

static int assert_deep_equal(xt_value a, xt_value b, int depth) {
  if (depth > 32) return xt_truthy(xt_seq(a, b));
  if (XT_IS_ARRAY(a) && XT_IS_ARRAY(b)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(a));
    if (count != (int32_t)xt_to_number(xt_array_length(b))) return 0;
    for (int32_t i = 0; i < count; i++) {
      if (!assert_deep_equal(xt_array_get(a, xt_number(i)), xt_array_get(b, xt_number(i)), depth + 1)) return 0;
    }
    return 1;
  }
  if (XT_IS_OBJECT(a) && XT_IS_OBJECT(b)) {
    xt_value keysA = xt_object_keys(a);
    xt_value keysB = xt_object_keys(b);
    int32_t count = (int32_t)xt_to_number(xt_array_length(keysA));
    if (count != (int32_t)xt_to_number(xt_array_length(keysB))) return 0;
    for (int32_t i = 0; i < count; i++) {
      xt_value key = xt_array_get(keysA, xt_number(i));
      if (!xt_truthy(xt_object_has(b, key))) return 0;
      if (!assert_deep_equal(xt_object_get(a, key), xt_object_get(b, key), depth + 1)) return 0;
    }
    return 1;
  }
  return xt_truthy(xt_seq(a, b));
}

/* -- assertions ----------------------------------------------------------- */

static xt_value assert_ok(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg(argc, argv, 0);
  if (!xt_truthy(value)) {
    assert_throw(value, XT_UNDEFINED, assert_message(xt_arg(argc, argv, 1), "The expression evaluated to a falsy value"));
  }
  return xt_undefined();
}

static xt_value assert_equality(int32_t argc, xt_value *argv, int strict, const char *headline, const char *operator) {
  xt_value actual = xt_arg(argc, argv, 0);
  xt_value expected = xt_arg(argc, argv, 1);
  xt_value message = xt_arg(argc, argv, 2);
  int equal = strict ? xt_truthy(xt_seq(actual, expected)) : xt_truthy(xt_eq(actual, expected));
  if (equal) return xt_undefined();
  xt_value text = xt_string_from_cstr(headline);
  text = xt_add(text, xt_to_string(actual));
  text = xt_add(text, xt_string_from_cstr(operator));
  text = xt_add(text, xt_to_string(expected));
  text = xt_add(text, xt_string_from_cstr("\n"));
  if (!xt_truthy(xt_is_nullish(message))) text = xt_to_string(message);
  assert_throw(actual, expected, text);
  return xt_undefined();
}

static xt_value assert_deep(int32_t argc, xt_value *argv, int negate) {
  xt_value actual = xt_arg(argc, argv, 0);
  xt_value expected = xt_arg(argc, argv, 1);
  xt_value message = xt_arg(argc, argv, 2);
  int equal = assert_deep_equal(actual, expected, 0);
  if (negate) equal = !equal;
  if (equal) return xt_undefined();
  xt_value text = xt_string_from_cstr(
    negate ? "Expected values to be strictly not deep-equal:\n\n" : "Expected values to be strictly deep-equal:\n\n");
  text = assert_join3(text, " !== ", expected);
  text = xt_add(text, xt_string_from_cstr("\n"));
  if (!xt_truthy(xt_is_nullish(message))) text = xt_to_string(message);
  assert_throw(actual, expected, text);
  return xt_undefined();
}

static xt_value assert_fail(int32_t argc, xt_value *argv) {
  xt_value message = xt_arg(argc, argv, 0);
  assert_throw(XT_UNDEFINED, XT_UNDEFINED, assert_message(message, "Failed"));
  return xt_undefined();
}

static xt_value assert_throws(int32_t argc, xt_value *argv, int negate) {
  xt_value fn = xt_arg(argc, argv, 0);
  xt_value message = xt_arg(argc, argv, 2);
  if (!XT_IS_FUNCTION(fn)) {
    assert_throw(XT_UNDEFINED, XT_UNDEFINED,
                 assert_message(message, negate ? "Expected a function" : "Expected a function to throw"));
  }
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    xt_closure_call(fn, 0, NULL);
    xt_try_leave(frame);
    if (negate) return xt_undefined();
    assert_throw(XT_UNDEFINED, XT_UNDEFINED,
                 assert_message(message, "Missing expected exception"));
  }
  xt_value error = xt_try_exception(frame);
  xt_try_leave(frame);
  if (negate) {
    xt_value text = xt_string_from_cstr("Got unwanted exception: ");
    text = xt_add(text, xt_to_string(error));
    assert_throw(XT_UNDEFINED, XT_UNDEFINED, xt_truthy(xt_is_nullish(message)) ? text : xt_to_string(message));
  }
  return xt_undefined();
}

static xt_value assert_if_error(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg(argc, argv, 0);
  if (!xt_truthy(xt_is_nullish(value))) {
    if (XT_IS_OBJECT(value)) xt_throw(value);
    assert_throw(XT_UNDEFINED, XT_UNDEFINED, xt_to_string(value));
  }
  return xt_undefined();
}

static xt_value assert_match(int32_t argc, xt_value *argv, int negate) {
  xt_value value = xt_arg(argc, argv, 0);
  xt_value pattern = xt_arg(argc, argv, 1);
  xt_value message = xt_arg(argc, argv, 2);
  xt_value text = xt_to_string(value);
  int matched = 0;
  if (xt_truthy(xt_is_nullish(pattern))) {
    matched = 0;
  } else if (XT_IS_STRING(pattern)) {
    matched = strstr(xt_string_data(text), xt_string_data(pattern)) != NULL;
  } else {
    /* A RegExp (or anything else) is delegated to String.prototype.match. */
    xt_value key = xt_string_from_cstr("match");
    xt_value result = xt_call_method(text, key, 1, &pattern);
    matched = !xt_truthy(xt_is_nullish(result));
  }
  if (negate) matched = !matched;
  if (matched) return xt_undefined();
  assert_throw(value, pattern, assert_message(message, "The input did not match the regular expression"));
  return xt_undefined();
}

xt_value xt_assert_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "ok") == 0) return assert_ok(argc, argv);
  if (strcmp(fn, "fail") == 0) return assert_fail(argc, argv);
  if (strcmp(fn, "equal") == 0) return assert_equality(argc, argv, 0, "Expected values to be loosely equal:\n\n", " != ");
  if (strcmp(fn, "notEqual") == 0) {
    /* `notEqual` succeeds when the values differ. */
    if (xt_truthy(xt_eq(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1)))) {
      assert_throw(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1),
                   assert_message(xt_arg(argc, argv, 2), "Expected values to not be loosely equal"));
    }
    return xt_undefined();
  }
  if (strcmp(fn, "strictEqual") == 0) return assert_equality(argc, argv, 1, "Expected values to be strictly equal:\n\n", " !== ");
  if (strcmp(fn, "notStrictEqual") == 0) {
    if (xt_truthy(xt_seq(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1)))) {
      assert_throw(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1),
                   assert_message(xt_arg(argc, argv, 2), "Expected values to be strictly not equal"));
    }
    return xt_undefined();
  }
  if (strcmp(fn, "deepEqual") == 0 || strcmp(fn, "deepStrictEqual") == 0) return assert_deep(argc, argv, 0);
  if (strcmp(fn, "notDeepEqual") == 0 || strcmp(fn, "notDeepStrictEqual") == 0) return assert_deep(argc, argv, 1);
  if (strcmp(fn, "throws") == 0) return assert_throws(argc, argv, 0);
  if (strcmp(fn, "doesNotThrow") == 0) return assert_throws(argc, argv, 1);
  if (strcmp(fn, "ifError") == 0) return assert_if_error(argc, argv);
  if (strcmp(fn, "match") == 0) return assert_match(argc, argv, 0);
  if (strcmp(fn, "doesNotMatch") == 0) return assert_match(argc, argv, 1);
  if (strcmp(fn, "rejects") == 0 || strcmp(fn, "doesNotReject") == 0) {
    /* No async rejection surface yet: resolve immediately. */
    return xt_promise_resolve(xt_undefined());
  }
  return xt_undefined();
}
