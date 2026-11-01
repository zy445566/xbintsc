/*
 * xbintsc runtime — promises and the microtask queue.
 *
 * Promise reactions are queued as microtasks and run when the program body
 * finishes, by the event loop between callbacks, and on demand. `await` lives
 * in `xt_async.c`: an `async` function runs on a coroutine, so awaiting a
 * promise that is still pending suspends it and registers a waiter here. When
 * the promise settles, the waiter becomes a microtask that resumes the
 * coroutine. Everything is single-threaded and non-blocking.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct xt_promise xt_promise;

typedef struct {
  xt_promise *promise;
  xt_value on_fulfilled;
  xt_value on_rejected;
  xt_promise *result;
  /* Non-NULL for an `async` coroutine waiting on this promise. Such a reaction
   * has no handlers: running it resumes the coroutine instead. */
  void *async;
} xt_reaction;

struct xt_promise {
  xt_header header;
  int state;
  xt_value value;
  xt_reaction **reactions;
  uint32_t reaction_count;
  uint32_t reaction_capacity;
  /* Unhandled-rejection tracking. `observed` is set once something can see
   * this promise's outcome: a `.then`/`.catch` registration, an `await`, or
   * adoption by another promise. `report_index` is its slot in `xt_unhandled`
   * (`XT_PROMISE_UNLISTED` while it is not queued). */
  int observed;
  uint32_t report_index;
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

/* -- unhandled rejections ------------------------------------------------- */
/*
 * A promise that rejects without anybody observing the outcome is a bug, and
 * an invisible one here: a compiled program's entry point is usually `async`,
 * so an escaping exception rejects *its* promise and nothing else looks at it.
 * The process used to print nothing and exit 0, which hides the failure.
 *
 * A rejected promise joins this queue and leaves it the moment a handler (or
 * an `await`) is registered — including one attached later, from a timer. What
 * is still queued once the event loop drains is reported on stderr and the
 * process exits non-zero, like Node's default `unhandled-rejections=throw`.
 */
#define XT_PROMISE_UNLISTED 0xFFFFFFFFu

static xt_promise **xt_unhandled = NULL;
static uint32_t xt_unhandled_count = 0;
static uint32_t xt_unhandled_capacity = 0;

static void xt_unhandled_remove(xt_promise *promise) {
  uint32_t index = promise->report_index;
  if (index == XT_PROMISE_UNLISTED) return;
  uint32_t last = xt_unhandled_count - 1;
  if (index != last) {
    xt_promise *moved = xt_unhandled[last];
    xt_unhandled[index] = moved;
    moved->report_index = index;
  }
  xt_unhandled_count = last;
  promise->report_index = XT_PROMISE_UNLISTED;
}

static void xt_unhandled_add(xt_promise *promise) {
  if (promise->observed || promise->report_index != XT_PROMISE_UNLISTED) return;
  if (xt_unhandled_count >= xt_unhandled_capacity) {
    xt_unhandled_capacity = xt_unhandled_capacity == 0 ? 8 : xt_unhandled_capacity * 2;
    xt_unhandled = (xt_promise **)realloc(xt_unhandled, sizeof(xt_promise *) * xt_unhandled_capacity);
    if (!xt_unhandled) abort();
  }
  promise->report_index = xt_unhandled_count;
  xt_unhandled[xt_unhandled_count++] = promise;
}

/* Something is listening now, so this promise can never be "unhandled". */
static void xt_promise_observe(xt_promise *promise) {
  if (!promise) return;
  promise->observed = 1;
  xt_unhandled_remove(promise);
}

/** Mark `value` (when it is a promise) as observed; see `xt_promise_observe`. */
void xt_promise_observe_value(xt_value value) { xt_promise_observe(xt_as_promise(value)); }

static void xt_promise_gc_register(void);

static xt_promise *xt_promise_new(void) {
  xt_promise *promise = (xt_promise *)xt_alloc(sizeof(xt_promise), XT_OBJECT_KIND_PROMISE);
  promise->state = XT_PROMISE_PENDING;
  promise->value = XT_UNDEFINED;
  promise->reactions = NULL;
  promise->reaction_count = 0;
  promise->reaction_capacity = 0;
  promise->observed = 0;
  promise->report_index = XT_PROMISE_UNLISTED;
  xt_promise_gc_register();
  return promise;
}

/* Reactions are plain `malloc` blocks referenced from promises and from the
 * microtask queue, so neither can be found by scanning the heap: the queue is
 * a root provider and a promise traces the reactions it still owns. */
static void xt_promise_gc_scan_roots(void) {
  for (uint32_t i = xt_microtask_head; i < xt_microtask_count; i++) {
    xt_reaction *reaction = xt_microtasks[i];
    if (!reaction) continue;
    xt_gc_mark_header((xt_header *)reaction->promise);
    xt_gc_mark_value(reaction->on_fulfilled);
    xt_gc_mark_value(reaction->on_rejected);
    xt_gc_mark_header((xt_header *)reaction->result);
    if (reaction->async) xt_gc_mark_header((xt_header *)reaction->async);
  }
  /* Pending rejections are reported by identity, and the reason must survive
   * long enough to be printed. */
  for (uint32_t i = 0; i < xt_unhandled_count; i++) xt_gc_mark_header((xt_header *)xt_unhandled[i]);
}

static void xt_promise_gc_register(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;
  xt_gc_register_root_provider(xt_promise_gc_scan_roots);
}

void xt_promise_gc_trace(xt_header *header) {
  xt_promise *promise = (xt_promise *)header;
  xt_gc_mark_value(promise->value);
  for (uint32_t i = 0; i < promise->reaction_count; i++) {
    xt_reaction *reaction = promise->reactions[i];
    if (!reaction) continue;
    xt_gc_mark_header((xt_header *)reaction->promise);
    xt_gc_mark_value(reaction->on_fulfilled);
    xt_gc_mark_value(reaction->on_rejected);
    xt_gc_mark_header((xt_header *)reaction->result);
    if (reaction->async) xt_gc_mark_header((xt_header *)reaction->async);
  }
}

void xt_promise_gc_release(xt_header *header) {
  xt_promise *promise = (xt_promise *)header;
  /* A listed rejection is a GC root, so this is only a safety net. */
  xt_unhandled_remove(promise);
  /* Pending reactions are owned solely by the promise; queued ones live in
   * `xt_microtasks` and are freed as they run. */
  for (uint32_t i = 0; i < promise->reaction_count; i++) free(promise->reactions[i]);
  free(promise->reactions);
}

static void xt_promise_settle(xt_promise *promise, int state, xt_value value);

static void xt_promise_resolve_into(xt_promise *result, xt_value value) {
  xt_promise *other = xt_as_promise(value);
  if (!other) {
    xt_promise_settle(result, XT_PROMISE_FULFILLED, value);
    return;
  }
  /* `result` follows `other`, so `other`'s rejection is consumed here even if
   * `result` ends up unhandled itself (Node reports only the outermost). */
  xt_promise_observe(other);
  if (other->state == XT_PROMISE_PENDING) {
    xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
    if (!reaction) abort();
    reaction->promise = other;
    reaction->on_fulfilled = XT_UNDEFINED;
    reaction->on_rejected = XT_UNDEFINED;
    reaction->result = result;
    reaction->async = NULL;
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
  if (state == XT_PROMISE_REJECTED) xt_unhandled_add(promise);
  for (uint32_t i = 0; i < promise->reaction_count; i++) xt_microtask_push(promise->reactions[i]);
  promise->reaction_count = 0;
}

static xt_value xt_promise_run_reactions(void) {
  while (xt_microtask_head < xt_microtask_count) {
    xt_reaction *reaction = xt_microtasks[xt_microtask_head++];
    xt_promise *promise = reaction->promise;
    if (reaction->async) {
      /* A suspended `async` coroutine: resume it with the settled value. It
       * runs until it completes or awaits another pending promise. */
      void *async = reaction->async;
      int rejected = promise->state == XT_PROMISE_REJECTED;
      xt_value settled = promise->value;
      free(reaction);
      xt_async_wake(async, settled, rejected);
      continue;
    }
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
    free(reaction);
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

/* -- helpers used by the async coroutine runtime -------------------------- */

xt_value xt_promise_create(void) { return xt_promise_value(xt_promise_new()); }

/* Queue a microtask that resumes a suspended async coroutine with an already
 * settled value. Reusing a reaction keeps FIFO order with pending `.then`
 * callbacks (the specification always yields once per `await`). */
void xt_promise_queue_async_wake(void *asyncPointer, xt_value settled, int rejected) {
  xt_promise *promise = xt_promise_new();
  promise->state = rejected ? XT_PROMISE_REJECTED : XT_PROMISE_FULFILLED;
  promise->value = settled;
  /* An internal handoff to a coroutine, never a rejection anyone can see. */
  promise->observed = 1;
  xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
  if (!reaction) abort();
  reaction->promise = promise;
  reaction->on_fulfilled = XT_UNDEFINED;
  reaction->on_rejected = XT_UNDEFINED;
  reaction->result = NULL;
  reaction->async = asyncPointer;
  xt_microtask_push(reaction);
}

void xt_promise_fulfill(xt_value promise, xt_value value) {
  xt_promise *target = xt_as_promise(promise);
  if (target) xt_promise_resolve_into(target, value);
}

void xt_promise_reject_value(xt_value promise, xt_value value) {
  xt_promise *target = xt_as_promise(promise);
  if (target) xt_promise_settle(target, XT_PROMISE_REJECTED, value);
}

int xt_promise_state_of(xt_value value) {
  xt_promise *promise = xt_as_promise(value);
  return promise ? promise->state : -1;
}

xt_value xt_promise_value_of(xt_value value) {
  xt_promise *promise = xt_as_promise(value);
  return promise ? promise->value : value;
}

void xt_promise_add_async_waiter(xt_value promiseValue, void *async) {
  xt_promise *promise = xt_as_promise(promiseValue);
  if (!promise || promise->state != XT_PROMISE_PENDING) return;
  /* The coroutine sees the rejection, so this promise is handled. */
  xt_promise_observe(promise);
  xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
  if (!reaction) abort();
  reaction->promise = promise;
  reaction->on_fulfilled = XT_UNDEFINED;
  reaction->on_rejected = XT_UNDEFINED;
  reaction->result = NULL;
  reaction->async = async;
  if (promise->reaction_count >= promise->reaction_capacity) {
    promise->reaction_capacity = promise->reaction_capacity == 0 ? 4 : promise->reaction_capacity * 2;
    promise->reactions = (xt_reaction **)realloc(promise->reactions, sizeof(xt_reaction *) * promise->reaction_capacity);
    if (!promise->reactions) abort();
  }
  promise->reactions[promise->reaction_count++] = reaction;
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

static xt_value xt_promise_with_resolvers_resolve(xt_value thisValue, xt_value env, int32_t argc,
                                                  xt_value *argv) {
  (void)thisValue;
  xt_promise_fulfill(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xt_promise_with_resolvers_reject(xt_value thisValue, xt_value env, int32_t argc,
                                                 xt_value *argv) {
  (void)thisValue;
  xt_promise_reject_value(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

xt_value xt_promise_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "withResolvers") == 0) {
    xt_value promise = xt_promise_create();
    xt_value environment[1] = {promise};
    xt_value result = xt_object_new();
    xt_object_set(result, xt_string_from_cstr("promise"), promise);
    xt_object_set(result, xt_string_from_cstr("resolve"),
                  xt_closure_new((void *)xt_promise_with_resolvers_resolve, 1, environment));
    xt_object_set(result, xt_string_from_cstr("reject"),
                  xt_closure_new((void *)xt_promise_with_resolvers_reject, 1, environment));
    return result;
  }
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
      xt_value reasons = xt_array_new(0, NULL);
      for (uint32_t i = 0; i < array->length; i++) {
        xt_value item = array->items[i];
        xt_promise *inner = xt_is_promise(item) ? xt_as_promise(item) : NULL;
        /* The combinator reads every element's outcome, including a rejection
         * it forwards to its own result. */
        xt_promise_observe(inner);
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
            xt_array_push(reasons, inner->value);
            continue;
          }
          xt_promise_settle(result, XT_PROMISE_REJECTED, inner->value);
          return xt_promise_value(result);
        }
        xt_value settled;
        if (xt_promise_state_of(item) == XT_PROMISE_FULFILLED) {
          settled = xt_promise_value_of(item);
        } else {
          settled = xt_await(item);
        }
        if (strcmp(fn, "any") == 0 || strcmp(fn, "race") == 0) {
          xt_promise_settle(result, XT_PROMISE_FULFILLED, settled);
          return xt_promise_value(result);
        }
        xt_array_push(values, settled);
      }
      if (strcmp(fn, "any") == 0 && anyRejected > 0) {
        xt_value ctorArgv[2];
        ctorArgv[0] = reasons;
        ctorArgv[1] = xt_string_from_cstr("All promises were rejected");
        xt_promise_settle(result, XT_PROMISE_REJECTED, xt_aggregate_error_ctor(2, ctorArgv));
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
    /* `.then` with no rejection handler still consumes this promise's
     * rejection: the derivative carries it on and is reported if nobody
     * handles *that* one, exactly like Node. */
    xt_promise_observe(promise);
    xt_promise *result = xt_promise_new();
    xt_reaction *reaction = (xt_reaction *)malloc(sizeof(xt_reaction));
    if (!reaction) abort();
    reaction->promise = promise;
    reaction->on_fulfilled = xt_arg_at(argc, argv, 0);
    reaction->on_rejected = xt_arg_at(argc, argv, 1);
    reaction->result = result;
    reaction->async = NULL;
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

xt_value xt_queue_microtask(int32_t argc, xt_value *argv) {
  xt_value callback = xt_arg_at(argc, argv, 0);
  if (!XT_IS_FUNCTION(callback)) {
    xt_value message = xt_string_from_cstr("The \"callback\" argument must be of type function");
    xt_throw(xt_type_error_ctor(1, &message));
    return XT_UNDEFINED;
  }
  /* Reuse the promise reaction queue: a settled promise's `then` callback is
   * exactly a microtask, and it keeps FIFO order with pending `.then`s. */
  xt_value promise = xt_promise_resolve(XT_UNDEFINED);
  int handled = 0;
  xt_promise_method(promise, "then", 1, &callback, &handled);
  return XT_UNDEFINED;
}

/* -- reporting ------------------------------------------------------------ */

int xt_promise_report_unhandled(void) {
  int reported = 0;
  for (uint32_t i = 0; i < xt_unhandled_count; i++) {
    xt_promise *promise = xt_unhandled[i];
    if (!promise || promise->observed) continue;
    reported++;
    /* `Error` (and its subclasses) carry a captured stack; everything else is
     * stringified the way Node prints a rejection reason. */
    xt_value text = xt_error_stack(promise->value);
    if (text == XT_UNDEFINED) text = xt_to_string(promise->value);
    xt_string *bytes = xt_as_string(text);
    if (bytes) {
      fprintf(stderr, "Unhandled promise rejection\n%s\n", xt_string_bytes(bytes));
    } else {
      fprintf(stderr, "Unhandled promise rejection\n");
    }
  }
  return reported;
}
