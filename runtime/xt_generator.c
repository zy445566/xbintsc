/*
 * xbintsc runtime — stackful generators and async generators.
 *
 * A generator function object carries `is_generator`. Calling it does not run
 * the body; it creates an `xt_generator` that owns a private stack. `.next()`
 * switches onto that stack, where the body runs until it calls `xt_yield`,
 * which switches back. Because the body keeps a real call stack, `try`,
 * `finally` and nested loops work across suspension points without a state
 * machine transform.
 *
 * An `async function*` uses the same coroutine, but every `.next()` /
 * `.throw()` / `.return()` returns a promise. The body may also suspend at an
 * `await`: `xt_generator_await` parks the coroutine on the awaited promise and
 * leaves the current `.next()` promise pending until the body yields or
 * completes. This is what makes `for await...of` work without a state machine.
 *
 * The private stack and the context switch come from `xt_coroutine.c`, which
 * `async` functions use as well. The generator's exception stack is swapped
 * alongside it so a `throw` inside the body still targets the body's own
 * `try` frames.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

/* One queued async-generator request. Requests are processed in call order, so
 * two `.next()` calls before the body yields do not overwrite each other. */
typedef struct {
  xt_value promise;
  xt_value sent;
  int kind; /* 0 = next, 1 = throw, 2 = return */
} xt_async_request;

typedef struct xt_generator {
  xt_header header;
  xt_value function;
  xt_value this_value;
  xt_value *args;
  int32_t argc;
  xt_value sent;   /* value passed by the most recent resume */
  xt_value result; /* yielded / returned value, or a thrown exception */
  /* Value pulled by `xt_iter_has` and consumed by `xt_iter_value`. */
  xt_value iter_value;
  int resume_kind; /* 0 = next, 1 = throw, 2 = return */
  int started;
  int done;
  int threw;
  /* Async-generator state. `pending_promise` belongs to the request currently
   * executing; `requests` holds calls made while it is pending. */
  int is_async;
  xt_value pending_promise;
  xt_value resume_value;
  int await_threw;
  xt_async_request *requests;
  uint32_t request_count;
  uint32_t request_capacity;
  /* Private stack, context switch and GC region bookkeeping live in
   * xt_coroutine.c; `NULL` once the body has finished. */
  xt_coroutine *coroutine;
} xt_generator;

/* Resolved async iterator for a *synchronous* iterable (`for await` over an
 * array). The source uses the ordinary index protocol; each step is wrapped in
 * an already-settled promise. */
typedef struct {
  xt_header header;
  xt_value iterator;
  xt_value index;
  int done;
} xt_async_iterator_state;

/* The generator whose coroutine is running, if any. */
static xt_generator *g_active_generator = NULL;

int xt_is_generator(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_GENERATOR;
}

int xt_is_async_generator(xt_value value) {
  if (!xt_is_generator(value)) return 0;
  return ((xt_generator *)XT_GET_PTR(value))->is_async;
}

static xt_generator *xt_as_generator(xt_value value) {
  if (!xt_is_generator(value)) return NULL;
  return (xt_generator *)XT_GET_PTR(value);
}

static xt_value xt_generator_result_object(xt_value value, int done) {
  xt_value result = xt_object_new();
  xt_object_set(result, xt_string_from_cstr("value"), value);
  xt_object_set(result, xt_string_from_cstr("done"), xt_bool(done));
  return result;
}

static xt_value xt_generator_done_promise(xt_value value) {
  return xt_promise_resolve(xt_generator_result_object(value, 1));
}

/* Runs on the generator's private stack. */
static void xt_generator_run(void *arg) {
  xt_generator *gen = (xt_generator *)arg;
  void *boundary = xt_try_enter();
  if (xt_try_setjmp(boundary) == 0) {
    xt_function *function = (xt_function *)XT_GET_PTR(gen->function);
    gen->result = function->code(gen->this_value, gen->function, gen->argc, gen->args);
    gen->threw = 0;
  } else {
    gen->result = xt_try_exception(boundary);
    gen->threw = 1;
  }
  gen->done = 1;
  xt_coroutine_leave(gen->coroutine);
}

/* Release the private stack once the coroutine has finished. The generator
 * object itself stays valid; later `.next()` calls just report `done`. */
static void xt_generator_dispose(xt_generator *gen) {
  xt_coroutine_release(gen->coroutine);
  gen->coroutine = NULL;
}

static void xt_generator_boot(xt_generator *gen);

/* Enter the coroutine. Returns with the coroutine suspended (yield/await) or
 * finished. */
static void xt_generator_enter(xt_generator *gen) {
  if (!gen->coroutine) xt_generator_boot(gen);
  xt_generator *previous = g_active_generator;
  g_active_generator = gen;
  xt_coroutine_enter(gen->coroutine);
  if (gen->done) xt_generator_dispose(gen);
  g_active_generator = previous;
}

/* Settle the promise of the request that just finished. */
static void xt_generator_settle_pending(xt_generator *gen) {
  if (!gen->is_async || gen->pending_promise == XT_UNDEFINED) return;
  xt_value promise = gen->pending_promise;
  gen->pending_promise = XT_UNDEFINED;
  if (gen->threw) xt_promise_reject_value(promise, gen->result);
  else xt_promise_fulfill(promise, xt_generator_result_object(gen->result, 1));
}

static xt_value xt_async_enqueue(xt_generator *gen, xt_value sent, int kind);

static void xt_generator_start_next(xt_generator *gen) {
  if (gen->request_count == 0) return;
  xt_async_request request = gen->requests[0];
  for (uint32_t i = 1; i < gen->request_count; i++) gen->requests[i - 1] = gen->requests[i];
  gen->request_count--;
  gen->pending_promise = request.promise;
  gen->sent = request.sent;
  gen->resume_kind = request.kind;

  if (gen->done) {
    if (request.kind == 1) xt_promise_reject_value(request.promise, request.sent);
    else xt_promise_fulfill(request.promise, xt_generator_result_object(request.sent, 1));
    gen->pending_promise = XT_UNDEFINED;
    return;
  }
  /* A `.throw()` before the body starts rejects the request; a `.return()`
   * before it starts completes the generator without running the body. */
  if (!gen->started) {
    if (request.kind == 1) {
      gen->done = 1;
      gen->threw = 1;
      gen->result = request.sent;
      xt_promise_reject_value(request.promise, request.sent);
      gen->pending_promise = XT_UNDEFINED;
      return;
    }
    if (request.kind == 2) {
      gen->done = 1;
      gen->threw = 0;
      gen->result = request.sent;
      xt_promise_fulfill(request.promise, xt_generator_result_object(request.sent, 1));
      gen->pending_promise = XT_UNDEFINED;
      return;
    }
  }
  gen->started = 1;
  xt_generator_enter(gen);
  if (gen->done) xt_generator_settle_pending(gen);
}

/* Drain queued requests whose predecessor has already finished. */
static void xt_generator_drain_requests(xt_generator *gen) {
  while (gen->pending_promise == XT_UNDEFINED && gen->request_count > 0) {
    xt_generator_start_next(gen);
  }
}

static xt_value xt_async_enqueue(xt_generator *gen, xt_value sent, int kind) {
  if (gen->done) {
    if (kind == 1) return xt_promise_reject(sent);
    return xt_generator_done_promise(kind == 2 ? sent : XT_UNDEFINED);
  }
  xt_value promise = xt_promise_create();
  if (gen->request_count >= gen->request_capacity) {
    uint32_t capacity = gen->request_capacity == 0 ? 4 : gen->request_capacity * 2;
    gen->requests = (xt_async_request *)realloc(gen->requests, sizeof(xt_async_request) * capacity);
    if (!gen->requests) abort();
    gen->request_capacity = capacity;
  }
  gen->requests[gen->request_count].promise = promise;
  gen->requests[gen->request_count].sent = sent;
  gen->requests[gen->request_count].kind = kind;
  gen->request_count++;
  if (gen->pending_promise == XT_UNDEFINED) xt_generator_drain_requests(gen);
  return promise;
}

/* The active generator is only reachable through this static pointer, and its
 * private stack holds values that are invisible to a scan of the main stack. */
static void xt_generator_gc_scan_roots(void) {
  if (!g_active_generator) return;
  xt_gc_mark_header((xt_header *)g_active_generator);
}

static void xt_generator_gc_register(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;
  xt_gc_register_root_provider(xt_generator_gc_scan_roots);
}

void xt_generator_gc_trace(xt_header *header) {
  xt_generator *gen = (xt_generator *)header;
  xt_gc_mark_value(gen->function);
  xt_gc_mark_value(gen->this_value);
  xt_gc_mark_value(gen->sent);
  xt_gc_mark_value(gen->result);
  xt_gc_mark_value(gen->iter_value);
  xt_gc_mark_value(gen->pending_promise);
  xt_gc_mark_value(gen->resume_value);
  if (gen->args && gen->argc > 0) {
    xt_gc_scan_region(gen->args, sizeof(xt_value) * (size_t)gen->argc);
  }
  for (uint32_t i = 0; i < gen->request_count; i++) {
    xt_gc_mark_value(gen->requests[i].promise);
    xt_gc_mark_value(gen->requests[i].sent);
  }
  xt_coroutine_gc_trace(gen->coroutine);
}

void xt_generator_gc_release(xt_header *header) {
  xt_generator *gen = (xt_generator *)header;
  free(gen->args);
  free(gen->requests);
  xt_coroutine_release(gen->coroutine);
  gen->coroutine = NULL;
}

static void xt_generator_boot(xt_generator *gen) {
  gen->coroutine = xt_coroutine_create(xt_generator_run, gen);
}

xt_value xt_generator_new(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv) {
  xt_generator *gen = (xt_generator *)xt_alloc(sizeof(xt_generator), XT_OBJECT_KIND_GENERATOR);
  xt_function *fn = (xt_function *)XT_GET_PTR(function);
  gen->function = function;
  gen->this_value = thisValue;
  gen->argc = argc;
  gen->args = NULL;
  gen->coroutine = NULL;
  gen->result = XT_UNDEFINED;
  gen->sent = XT_UNDEFINED;
  gen->iter_value = XT_UNDEFINED;
  gen->resume_kind = 0;
  gen->started = 0;
  gen->done = 0;
  gen->threw = 0;
  gen->is_async = fn->is_async_generator;
  gen->pending_promise = XT_UNDEFINED;
  gen->resume_value = XT_UNDEFINED;
  gen->await_threw = 0;
  gen->requests = NULL;
  gen->request_count = 0;
  gen->request_capacity = 0;
  if (argc > 0 && argv) {
    gen->args = (xt_value *)malloc(sizeof(xt_value) * (size_t)argc);
    if (!gen->args) abort();
    memcpy(gen->args, argv, sizeof(xt_value) * (size_t)argc);
  }
  xt_generator_gc_register();
  return XT_FROM_PTR(XT_TAG_OBJECT, gen);
}

xt_value xt_generator_next(xt_value generator, xt_value sent) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen) {
    xt_throw(xt_error_from_message("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
  if (gen->is_async) return xt_async_enqueue(gen, sent, 0);
  if (gen->done) return xt_generator_result_object(XT_UNDEFINED, 1);
  gen->sent = sent;
  gen->resume_kind = 0;
  gen->started = 1;
  xt_generator_enter(gen);
  if (gen->done) {
    if (gen->threw) xt_throw(gen->result);
    return xt_generator_result_object(gen->result, 1);
  }
  return xt_generator_result_object(gen->result, 0);
}

xt_value xt_generator_throw(xt_value generator, xt_value error) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen) {
    xt_throw(xt_error_from_message("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
  if (gen->is_async) return xt_async_enqueue(gen, error, 1);
  if (gen->done) {
    xt_throw(error);
    return XT_UNDEFINED;
  }
  gen->sent = error;
  gen->resume_kind = 1;
  gen->started = 1;
  xt_generator_enter(gen);
  if (gen->done) {
    if (gen->threw) xt_throw(gen->result);
    return xt_generator_result_object(gen->result, 1);
  }
  return xt_generator_result_object(gen->result, 0);
}

xt_value xt_generator_return(xt_value generator, xt_value value) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen) {
    xt_throw(xt_error_from_message("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
  if (gen->is_async) return xt_async_enqueue(gen, value, 2);
  /* Abandon the coroutine without resuming: finally blocks are not run. */
  gen->done = 1;
  gen->threw = 0;
  gen->result = value;
  xt_generator_dispose(gen);
  return xt_generator_result_object(value, 1);
}

/* Suspend the running generator with `value`; returns the value the caller
 * passes to the next resume (`next(v)` / `throw(e)` / `return(v)`). */
xt_value xt_yield(xt_value value) {
  xt_generator *gen = g_active_generator;
  if (!gen || gen->done) return XT_UNDEFINED;
  gen->result = value;
  /* Settle the async `.next()` request that produced this yield. A pending
   * `.throw(e)` must *not* settle it: if the body catches the error it may
   * yield again, and only that next yield (or the escaped exception) settles
   * the request. */
  if (gen->is_async && gen->resume_kind != 1 && gen->pending_promise != XT_UNDEFINED) {
    xt_value promise = gen->pending_promise;
    gen->pending_promise = XT_UNDEFINED;
    xt_promise_fulfill(promise, xt_generator_result_object(value, gen->resume_kind == 2));
  }
  /* The resume kind may change while suspended: control returns here only
   * when the next `.next()` / `.throw()` / `.return()` resumes the coroutine,
   * so the checks below must sit after the context switch. */
  xt_coroutine_leave(gen->coroutine);
  if (gen->resume_kind == 1) {
    gen->resume_kind = 0;
    xt_throw(gen->sent);
    return XT_UNDEFINED;
  }
  if (gen->resume_kind == 2) {
    gen->resume_kind = 0;
    gen->done = 1;
    gen->threw = 0;
    gen->result = gen->sent;
    if (gen->is_async && gen->pending_promise != XT_UNDEFINED) {
      xt_value promise = gen->pending_promise;
      gen->pending_promise = XT_UNDEFINED;
      xt_promise_fulfill(promise, xt_generator_result_object(gen->sent, 1));
    }
    xt_coroutine_leave(gen->coroutine);
    return XT_UNDEFINED;
  }
  return gen->sent;
}

/* `await value` inside an async generator. The generator's current `.next()`
 * promise stays pending; when the awaited promise settles the coroutine is
 * resumed and this function returns the settlement (or throws a rejection). */
int xt_generator_await(xt_value value, xt_value *result) {
  xt_generator *gen = g_active_generator;
  if (!gen || !gen->is_async || gen->done || !gen->coroutine) return 0;
  /* A nested async function inside the generator runs on its own coroutine;
   * its awaits must not suspend the generator. */
  if (!xt_coroutine_is_current(gen->coroutine)) return 0;
  int state = xt_promise_state_of(value);
  if (state == XT_PROMISE_PENDING) {
    gen->await_threw = 0;
    gen->resume_value = XT_UNDEFINED;
    xt_promise_add_async_waiter(value, gen);
    xt_coroutine_leave(gen->coroutine);
  } else {
    int rejected = state == XT_PROMISE_REJECTED;
    xt_value settled = (state == XT_PROMISE_FULFILLED || rejected)
                           ? xt_promise_value_of(value)
                           : value;
    gen->await_threw = rejected;
    gen->resume_value = settled;
    xt_promise_queue_async_wake(gen, settled, rejected);
    xt_coroutine_leave(gen->coroutine);
  }
  if (gen->await_threw) {
    gen->await_threw = 0;
    xt_throw(gen->resume_value);
    return 1;
  }
  *result = gen->resume_value;
  return 1;
}

void xt_generator_wake(void *pointer, xt_value value, int rejected) {
  xt_generator *gen = (xt_generator *)pointer;
  if (!gen || gen->done || !gen->coroutine) return;
  gen->resume_value = value;
  gen->await_threw = rejected;
  xt_generator_enter(gen);
  if (gen->done) xt_generator_settle_pending(gen);
  else xt_generator_drain_requests(gen);
}

/* `yield* delegate`: relay every value the delegate produces. */
xt_value xt_yield_star(xt_value delegate) {
  xt_value nextName = xt_string_from_cstr("next");
  int usesNext = XT_IS_FUNCTION(xt_get(delegate, nextName));
  /* A plain iterable (`[Symbol.iterator]` but no `next`) resolves through the
   * iteration protocol; arrays/strings/Map/Set keep their index protocol. */
  if (!usesNext) delegate = xt_iter_open(delegate);
  xt_value sent = XT_UNDEFINED;
  int32_t index = 0;
  for (;;) {
    xt_value value;
    int done;
    if (usesNext) {
      xt_value step = xt_call_method(delegate, nextName, 1, &sent);
      done = xt_truthy(xt_get(step, xt_string_from_cstr("done")));
      value = xt_get(step, xt_string_from_cstr("value"));
    } else {
      xt_value position = xt_number((double)index);
      if (!xt_truthy(xt_iter_has(delegate, position))) return XT_UNDEFINED;
      value = xt_iter_value(delegate, position);
      index++;
      done = 0;
    }
    if (done) return value;
    sent = xt_yield(value);
  }
}

/* `for...of` step for generators: pull and cache the next value. */
int xt_generator_has_next(xt_value generator) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen || gen->done || gen->is_async) return 0;
  xt_value step = xt_generator_next(generator, XT_UNDEFINED);
  if (xt_truthy(xt_get(step, xt_string_from_cstr("done")))) return 0;
  gen->iter_value = xt_get(step, xt_string_from_cstr("value"));
  return 1;
}

xt_value xt_generator_iter_value(xt_value generator) {
  xt_generator *gen = xt_as_generator(generator);
  return gen ? gen->iter_value : XT_UNDEFINED;
}

xt_value xt_function_set_generator(xt_value value) {
  if (XT_IS_FUNCTION(value)) ((xt_function *)XT_GET_PTR(value))->is_generator = 1;
  return value;
}

xt_value xt_function_set_async_generator(xt_value value) {
  if (XT_IS_FUNCTION(value)) {
    xt_function *function = (xt_function *)XT_GET_PTR(value);
    function->is_generator = 1;
    function->is_async_generator = 1;
  }
  return value;
}

/* -- async iteration ------------------------------------------------------ */

static xt_value xt_async_iterator_self(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

xt_value xt_async_iterator_method(void) {
  static xt_value method = XT_UNDEFINED;
  if (!XT_IS_FUNCTION(method)) {
    method = xt_closure_new((void *)xt_async_iterator_self, 0, NULL);
    xt_gc_add_root(&method);
  }
  return method;
}

xt_value xt_async_iter_open(xt_value value) {
  if (xt_is_async_generator(value)) return value;
  /* A user object may implement `[Symbol.asyncIterator]()`. */
  if (XT_IS_OBJECT(value) || XT_IS_ARRAY(value) || XT_IS_FUNCTION(value)) {
    xt_value method = xt_get(value, xt_symbol_well_known("asyncIterator"));
    if (XT_IS_FUNCTION(method)) {
      xt_value iterator = xt_call_with_this(method, value, 0, NULL);
      if (XT_IS_OBJECT(iterator) || XT_IS_FUNCTION(iterator)) return iterator;
    }
  }
  /* Fall back to the synchronous protocol and adapt each step. */
  xt_async_iterator_state *state =
      (xt_async_iterator_state *)xt_alloc(sizeof(xt_async_iterator_state),
                                          XT_OBJECT_KIND_ASYNC_ITERATOR);
  state->iterator = xt_iter_open(value);
  state->index = xt_number(0);
  state->done = 0;
  return XT_FROM_PTR(XT_TAG_OBJECT, state);
}

static int xt_is_async_iterator_state(xt_value value) {
  return XT_IS_OBJECT(value) &&
         ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_ASYNC_ITERATOR;
}

xt_value xt_async_iter_next(xt_value iterator) {
  if (xt_is_async_generator(iterator)) return xt_generator_next(iterator, XT_UNDEFINED);
  if (xt_is_async_iterator_state(iterator)) {
    xt_async_iterator_state *state = (xt_async_iterator_state *)XT_GET_PTR(iterator);
    if (state->done) return xt_promise_resolve(xt_generator_result_object(XT_UNDEFINED, 1));
    if (!xt_truthy(xt_iter_has(state->iterator, state->index))) {
      state->done = 1;
      return xt_promise_resolve(xt_generator_result_object(XT_UNDEFINED, 1));
    }
    xt_value value = xt_iter_value(state->iterator, state->index);
    state->index = xt_add(state->index, xt_number(1));
    return xt_promise_resolve(xt_generator_result_object(value, 0));
  }
  xt_value step = xt_call_method(iterator, xt_string_from_cstr("next"), 0, NULL);
  if (xt_promise_state_of(step) >= 0) return step;
  return xt_promise_resolve(step);
}

void xt_async_iterator_gc_trace(xt_header *header) {
  xt_async_iterator_state *state = (xt_async_iterator_state *)header;
  xt_gc_mark_value(state->iterator);
  xt_gc_mark_value(state->index);
}
