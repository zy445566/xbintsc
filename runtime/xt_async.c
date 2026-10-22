/*
 * xbintsc runtime — real `async` / `await`.
 *
 * An `async` function runs its body on a stackful coroutine (the same
 * machinery generators use, see `xt_coroutine.c`) and immediately returns a
 * promise. The body runs synchronously until it reaches an `await` whose
 * operand is still pending; at that point the coroutine suspends and control
 * returns to the caller, so the program body or the event loop can keep going.
 * When the awaited promise settles, the promise runtime queues a microtask that
 * resumes the coroutine exactly where it stopped — because the coroutine keeps
 * a real C stack, `try`/`finally`, loops and locals all survive the suspension
 * without a state-machine transform.
 *
 * `await` always yields: even a non-promise or an already-settled promise
 * queues a microtask before the body resumes, matching the specification's
 * interleaving with pending `.then` callbacks. (Top-level `await`, with no
 * coroutine to suspend, still blocks on the event loop.)
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct xt_async {
  xt_header header;
  xt_value function;
  xt_value this_value;
  xt_value *args;
  int32_t argc;
  /* Promise returned to the caller and settled when the body finishes. */
  xt_value promise;
  /* Completion value, or the exception that escaped the body. */
  xt_value value;
  /* Values handed over by the waiter and consumed by `xt_await`. */
  xt_value resume_value;
  int await_threw;
  int done;
  int threw;
  xt_coroutine *coroutine;
} xt_async;

/* The async coroutine currently running on its private stack, if any. It is
 * only reachable through this pointer while it runs, so it is a GC root. */
static xt_async *g_active_async = NULL;

static void xt_async_gc_register(void);

/* Runs on the coroutine's private stack and never returns normally: it
 * suspends back to the resumer once the body has completed. */
static void xt_async_run(void *arg) {
  xt_async *async = (xt_async *)arg;
  void *boundary = xt_try_enter();
  if (xt_try_setjmp(boundary) == 0) {
    xt_function *function = (xt_function *)XT_GET_PTR(async->function);
    async->value = function->code(async->this_value, async->function, async->argc, async->args);
    async->threw = 0;
  } else {
    /* `throw` inside the body (not caught by a `try`) rejects the promise. */
    async->value = xt_try_exception(boundary);
    async->threw = 1;
  }
  async->done = 1;
  xt_coroutine_leave(async->coroutine);
}

/* Switch onto the coroutine. It either suspends at an `await` or completes. */
static void xt_async_resume(xt_async *async) {
  xt_async *previous = g_active_async;
  g_active_async = async;
  xt_coroutine_enter(async->coroutine);
  g_active_async = previous;
  if (!async->done) return;
  xt_coroutine_release(async->coroutine);
  async->coroutine = NULL;
  if (async->threw) xt_promise_reject_value(async->promise, async->value);
  else xt_promise_fulfill(async->promise, async->value);
}

xt_value xt_async_call(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv) {
  xt_async *async = (xt_async *)xt_alloc(sizeof(xt_async), XT_OBJECT_KIND_ASYNC);
  async->function = function;
  async->this_value = thisValue;
  async->argc = argc;
  async->args = NULL;
  if (argc > 0 && argv) {
    async->args = (xt_value *)malloc(sizeof(xt_value) * (size_t)argc);
    if (!async->args) abort();
    memcpy(async->args, argv, sizeof(xt_value) * (size_t)argc);
  }
  async->promise = xt_promise_create();
  async->value = XT_UNDEFINED;
  async->resume_value = XT_UNDEFINED;
  async->await_threw = 0;
  async->done = 0;
  async->threw = 0;
  async->coroutine = xt_coroutine_create(xt_async_run, async);
  xt_async_gc_register();
  /* Start synchronously, matching JavaScript: the body runs up to its first
   * pending `await` before the call returns. */
  xt_async_resume(async);
  return async->promise;
}

void xt_async_wake(void *asyncPointer, xt_value value, int rejected) {
  xt_async *async = (xt_async *)asyncPointer;
  if (!async || async->done || !async->coroutine) return;
  async->resume_value = value;
  async->await_threw = rejected;
  xt_async_resume(async);
}

/* `await value`: every await yields once. A settled promise (or a plain value)
 * queues a microtask before resuming, so an await always interleaves with
 * pending `.then` callbacks exactly like the specification. */
xt_value xt_await(xt_value value) {
  int state = xt_promise_state_of(value);
  xt_async *async = g_active_async;
  if (async && async->coroutine) {
    if (state == XT_PROMISE_PENDING) {
      async->await_threw = 0;
      async->resume_value = XT_UNDEFINED;
      xt_promise_add_async_waiter(value, async);
      xt_coroutine_leave(async->coroutine);
    } else {
      int rejected = state == XT_PROMISE_REJECTED;
      xt_value settled = (state == XT_PROMISE_FULFILLED || rejected)
                             ? xt_promise_value_of(value)
                             : value;
      async->await_threw = rejected;
      async->resume_value = settled;
      xt_promise_queue_async_wake(async, settled, rejected);
      xt_coroutine_leave(async->coroutine);
    }
    /* The waiter/queued microtask resumed us. A rejection is rethrown inside
     * the coroutine so its own `try`/`catch` and `finally` see it. */
    if (async->await_threw) {
      async->await_threw = 0;
      xt_throw(async->resume_value);
      return XT_UNDEFINED;
    }
    return async->resume_value;
  }

  if (state < 0) return value;
  if (state == XT_PROMISE_REJECTED) {
    xt_throw(xt_promise_value_of(value));
    return XT_UNDEFINED;
  }
  if (state == XT_PROMISE_FULFILLED) return xt_promise_value_of(value);

  /* No coroutine to suspend (top-level `await`, or `await` reached outside an
   * async function): block on the reactor until the promise settles, exactly
   * like a synchronous embedder waiting for the event loop. The loop returns
   * immediately when no work remains, so a promise nothing can settle does not
   * hang the process here. */
  for (int guard = 0; guard < 100000; guard++) {
    xt_drain_microtasks();
    state = xt_promise_state_of(value);
    if (state != XT_PROMISE_PENDING) break;
    if (!xt_loop_has_work()) break;
    if (xt_loop_poll(-1) < 0) break;
  }
  state = xt_promise_state_of(value);
  if (state == XT_PROMISE_REJECTED) {
    xt_throw(xt_promise_value_of(value));
    return XT_UNDEFINED;
  }
  return state == XT_PROMISE_FULFILLED ? xt_promise_value_of(value) : XT_UNDEFINED;
}

/* While a coroutine runs, the async object is only reachable through this
 * static pointer. */
static void xt_async_gc_scan_roots(void) {
  if (!g_active_async) return;
  xt_gc_mark_header((xt_header *)g_active_async);
}

static void xt_async_gc_register(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;
  xt_gc_register_root_provider(xt_async_gc_scan_roots);
}

void xt_async_gc_trace(xt_header *header) {
  xt_async *async = (xt_async *)header;
  xt_gc_mark_value(async->function);
  xt_gc_mark_value(async->this_value);
  xt_gc_mark_value(async->promise);
  xt_gc_mark_value(async->value);
  xt_gc_mark_value(async->resume_value);
  if (async->args && async->argc > 0) {
    xt_gc_scan_region(async->args, sizeof(xt_value) * (size_t)async->argc);
  }
  xt_coroutine_gc_trace(async->coroutine);
}

void xt_async_gc_release(xt_header *header) {
  xt_async *async = (xt_async *)header;
  free(async->args);
  xt_coroutine_release(async->coroutine);
  async->coroutine = NULL;
}

xt_value xt_function_set_async(xt_value value) {
  if (XT_IS_FUNCTION(value)) ((xt_function *)XT_GET_PTR(value))->is_async = 1;
  return value;
}
