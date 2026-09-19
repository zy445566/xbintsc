/*
 * xbintsc runtime — promises and async support.
 *
 * There is no event loop in the runtime, so promises use a microtask queue
 * that the compiler drains when the program body finishes (and that `await`
 * drains on demand). This gives synchronous-looking `async`/`await` semantics
 * for the common case where every awaited value is already settled.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#define XT_PROMISE_PENDING 0
#define XT_PROMISE_FULFILLED 1
#define XT_PROMISE_REJECTED 2

typedef struct xt_promise xt_promise;

typedef struct {
  xt_promise *promise;
  xt_value on_fulfilled;
  xt_value on_rejected;
  xt_promise *result;
} xt_reaction;

struct xt_promise {
  xt_header header;
  int state;
  xt_value value;
  xt_reaction **reactions;
  uint32_t reaction_count;
  uint32_t reaction_capacity;
};

/* -- microtask queue ------------------------------------------------------ */

static xt_reaction **xt_microtasks = NULL;
static uint32_t xt_microtask_count = 0;
static uint32_t xt_microtask_head = 0;
static uint32_t xt_microtask_capacity = 0;

static void xt_microtask_push(xt_reaction *reaction) {
  if (xt_microtask_count >= xt_microtask_capacity) {
    xt_microtask_capacity = xt_microtask_capacity == 0 ? 16 : xt_microtask_capacity * 2;
    xt_microtasks = (xt_reaction **)realloc(xt_microtasks, sizeof(xt_reaction *) * xt_microtask_capacity);
    if (!xt_microtasks) abort();
  }
  xt_microtasks[xt_microtask_count++] = reaction;
}

int xt_is_promise(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_PROMISE;
}

static xt_promise *xt_as_promise(xt_value value) {
  if (!xt_is_promise(value)) return NULL;
  return (xt_promise *)XT_GET_PTR(value);
}

static xt_value xt_promise_value(xt_promise *promise) {
  return XT_FROM_PTR(XT_TAG_OBJECT, promise);
}

static xt_promise *xt_promise_new(void) {
  xt_promise *promise = (xt_promise *)xt_alloc(sizeof(xt_promise), XT_OBJECT_KIND_PROMISE);
  promise->state = XT_PROMISE_PENDING;
  promise->value = XT_UNDEFINED;
  promise->reactions = NULL;
  promise->reaction_count = 0;
  promise->reaction_capacity = 0;
  return promise;
}

static void xt_promise_settle(xt_promise *promise, int state, xt_value value);

static void xt_promise_resolve_into(xt_promise *result, xt_value value) {
  xt_promise *other = xt_as_promise(value);
  if (!other) {
    xt_promise_settle(result, XT_PROMISE_FULFILLED, value);
    return;
  }
  if (other->state == XT_PROMISE_PENDING) {
    xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
    if (!reaction) abort();
    reaction->promise = other;
    reaction->on_fulfilled = XT_UNDEFINED;
    reaction->on_rejected = XT_UNDEFINED;
    reaction->result = result;
    if (other->reaction_count >= other->reaction_capacity) {
      other->reaction_capacity = other->reaction_capacity == 0 ? 4 : other->reaction_capacity * 2;
      other->reactions = (xt_reaction **)realloc(other->reactions, sizeof(xt_reaction *) * other->reaction_capacity);
      if (!other->reactions) abort();
    }
    other->reactions[other->reaction_count++] = reaction;
  } else {
    xt_promise_settle(result, other->state, other->value);
  }
}

static void xt_promise_settle(xt_promise *promise, int state, xt_value value) {
  if (promise->state != XT_PROMISE_PENDING) return;
  promise->state = state;
  promise->value = value;
  for (uint32_t i = 0; i < promise->reaction_count; i++) xt_microtask_push(promise->reactions[i]);
  promise->reaction_count = 0;
}

static xt_value xt_promise_run_reactions(void) {
  while (xt_microtask_head < xt_microtask_count) {
    xt_reaction *reaction = xt_microtasks[xt_microtask_head++];
    xt_promise *promise = reaction->promise;
    xt_value handler = promise->state == XT_PROMISE_FULFILLED ? reaction->on_fulfilled : reaction->on_rejected;
    if (XT_IS_FUNCTION(handler)) {
      xt_value arg = promise->value;
      xt_value out;
      out = xt_closure_call(handler, 1, &arg);
      xt_promise_resolve_into(reaction->result, out);
    } else if (promise->state == XT_PROMISE_FULFILLED) {
      xt_promise_settle(reaction->result, XT_PROMISE_FULFILLED, promise->value);
    } else {
      xt_promise_settle(reaction->result, XT_PROMISE_REJECTED, promise->value);
    }
  }
  xt_microtask_head = 0;
  xt_microtask_count = 0;
  return XT_UNDEFINED;
}

void xt_drain_microtasks(void) {
  for (int guard = 0; guard < 100000; guard++) {
    if (xt_microtask_head >= xt_microtask_count) break;
    xt_promise_run_reactions();
  }
}

xt_value xt_await(xt_value value) {
  xt_promise *promise = xt_as_promise(value);
  if (!promise) return value;
  int guard = 0;
  while (promise->state == XT_PROMISE_PENDING && guard++ < 100000) xt_drain_microtasks();
  if (promise->state == XT_PROMISE_REJECTED) {
    xt_throw(promise->value);
    return XT_UNDEFINED;
  }
  return promise->value;
}

/* -- C-callable resolve / reject ------------------------------------------ */

static xt_value xt_promise_resolve_cb(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value promiseValue = xt_closure_env(env, 0);
  xt_promise *promise = xt_as_promise(promiseValue);
  if (promise) xt_promise_resolve_into(promise, xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xt_promise_reject_cb(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value promiseValue = xt_closure_env(env, 0);
  xt_promise *promise = xt_as_promise(promiseValue);
  if (promise) xt_promise_settle(promise, XT_PROMISE_REJECTED, xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xt_make_callback(xt_code_fn fn, xt_value promiseValue) {
  xt_value env[1] = {promiseValue};
  return xt_closure_new((void *)fn, 1, env);
}

xt_value xt_promise_ctor(int32_t argc, xt_value *argv) {
  xt_promise *promise = xt_promise_new();
  xt_value promiseValue = xt_promise_value(promise);
  xt_value executor = xt_arg_at(argc, argv, 0);
  if (XT_IS_FUNCTION(executor)) {
    xt_value args[2];
    args[0] = xt_make_callback(xt_promise_resolve_cb, promiseValue);
    args[1] = xt_make_callback(xt_promise_reject_cb, promiseValue);
    xt_closure_call(executor, 2, args);
  }
  return promiseValue;
}

xt_value xt_promise_resolve(xt_value value) {
  xt_promise *existing = xt_as_promise(value);
  if (existing) return value;
  xt_promise *promise = xt_promise_new();
  xt_promise_resolve_into(promise, value);
  return xt_promise_value(promise);
}

xt_value xt_promise_reject(xt_value value) {
  xt_promise *promise = xt_promise_new();
  xt_promise_settle(promise, XT_PROMISE_REJECTED, value);
  return xt_promise_value(promise);
}

xt_value xt_promise_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "resolve") == 0) return xt_promise_resolve(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "reject") == 0) return xt_promise_reject(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "all") == 0 || strcmp(fn, "allSettled") == 0 || strcmp(fn, "race") == 0 || strcmp(fn, "any") == 0) {
    xt_value iterable = xt_arg_at(argc, argv, 0);
    int all = strcmp(fn, "all") == 0 || strcmp(fn, "allSettled") == 0;
    xt_promise *result = xt_promise_new();
    xt_value values = xt_array_new(0, NULL);
    if (XT_IS_ARRAY(iterable)) {
      xt_array *array = xt_as_array(iterable);
      int anyRejected = 0;
      for (uint32_t i = 0; i < array->length; i++) {
        xt_value item = array->items[i];
        xt_promise *inner = xt_is_promise(item) ? xt_as_promise(item) : NULL;
        int rejected = inner && inner->state == XT_PROMISE_REJECTED;
        if (strcmp(fn, "allSettled") == 0) {
          xt_value record = xt_object_new();
          xt_object_set(record, xt_string_from_cstr("status"), xt_string_from_cstr(rejected ? "rejected" : "fulfilled"));
          xt_object_set(record, xt_string_from_cstr(rejected ? "reason" : "value"), rejected ? inner->value : item);
          xt_array_push(values, record);
          continue;
        }
        if (rejected) {
          if (strcmp(fn, "any") == 0) {
            anyRejected++;
            continue;
          }
          xt_promise_settle(result, XT_PROMISE_REJECTED, inner->value);
          return xt_promise_value(result);
        }
        xt_value settled = xt_await(item);
        if (strcmp(fn, "any") == 0 || strcmp(fn, "race") == 0) {
          xt_promise_settle(result, XT_PROMISE_FULFILLED, settled);
          return xt_promise_value(result);
        }
        xt_array_push(values, settled);
      }
      if (strcmp(fn, "any") == 0 && anyRejected > 0) {
        xt_promise_settle(result, XT_PROMISE_REJECTED, xt_string_from_cstr("AggregateError: all promises rejected"));
        return xt_promise_value(result);
      }
    }
    if (all) xt_promise_settle(result, XT_PROMISE_FULFILLED, values);
    else xt_promise_settle(result, XT_PROMISE_REJECTED, xt_string_from_cstr("AggregateError: no promises"));
    return xt_promise_value(result);
  }
  return XT_UNDEFINED;
}

/* -- instance methods ----------------------------------------------------- */

xt_value xt_promise_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_promise *promise = xt_as_promise(target);
  if (!promise) {
    *handled = 0;
    return XT_UNDEFINED;
  }
  if (strcmp(method, "then") == 0) {
    xt_promise *result = xt_promise_new();
    xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
    if (!reaction) abort();
    reaction->promise = promise;
    reaction->on_fulfilled = xt_arg_at(argc, argv, 0);
    reaction->on_rejected = xt_arg_at(argc, argv, 1);
    reaction->result = result;
    if (promise->state == XT_PROMISE_PENDING) {
      if (promise->reaction_count >= promise->reaction_capacity) {
        promise->reaction_capacity = promise->reaction_capacity == 0 ? 4 : promise->reaction_capacity * 2;
        promise->reactions = (xt_reaction **)realloc(promise->reactions, sizeof(xt_reaction *) * promise->reaction_capacity);
        if (!promise->reactions) abort();
      }
      promise->reactions[promise->reaction_count++] = reaction;
    } else {
      xt_microtask_push(reaction);
    }
    return xt_promise_value(result);
  }
  if (strcmp(method, "catch") == 0) {
    xt_value args[2] = {XT_UNDEFINED, xt_arg_at(argc, argv, 0)};
    return xt_promise_method(target, "then", 2, args, handled);
  }
  if (strcmp(method, "finally") == 0) {
    xt_value fn = xt_arg_at(argc, argv, 0);
    /* Approximate: run the handler, then propagate the original value. */
    if (XT_IS_FUNCTION(fn)) xt_closure_call(fn, 0, NULL);
    return target;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
