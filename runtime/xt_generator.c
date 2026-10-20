/*
 * xbintsc runtime — stackful generators.
 *
 * A generator function object carries `is_generator`. Calling it does not run
 * the body; it creates an `xt_generator` that owns a private stack. `.next()`
 * switches onto that stack, where the body runs until it calls `xt_yield`,
 * which switches back. Because the body keeps a real call stack, `try`,
 * `finally` and nested loops work across suspension points without a state
 * machine transform.
 *
 * The private stack and the context switch come from `xt_coroutine.c`, which
 * `async` functions use as well. The generator's exception stack is swapped
 * alongside it so a `throw` inside the body still targets the body's own
 * `try` frames.
 */

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

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
  /* Private stack, context switch and GC region bookkeeping live in
   * xt_coroutine.c; `NULL` once the body has finished. */
  xt_coroutine *coroutine;
} xt_generator;

/* The generator whose coroutine is running, if any. */
static xt_generator *g_active_generator = NULL;

int xt_is_generator(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_GENERATOR;
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

static void xt_generator_resume(xt_generator *gen) {
  if (!gen->coroutine) xt_generator_boot(gen);
  xt_generator *previous = g_active_generator;
  g_active_generator = gen;
  xt_coroutine_enter(gen->coroutine);
  if (gen->done) xt_generator_dispose(gen);
  g_active_generator = previous;
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
  size_t payload = header->size > sizeof(xt_header) ? header->size - sizeof(xt_header) : 0;
  xt_gc_scan_region((const char *)header + sizeof(xt_header), payload);
  if (gen->args && gen->argc > 0) {
    xt_gc_scan_region(gen->args, sizeof(xt_value) * (size_t)gen->argc);
  }
  xt_coroutine_gc_trace(gen->coroutine);
}

void xt_generator_gc_release(xt_header *header) {
  xt_generator *gen = (xt_generator *)header;
  free(gen->args);
  xt_coroutine_release(gen->coroutine);
  gen->coroutine = NULL;
}

static void xt_generator_boot(xt_generator *gen) {
  gen->coroutine = xt_coroutine_create(xt_generator_run, gen);
}

xt_value xt_generator_new(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv) {
  xt_generator *gen = (xt_generator *)xt_alloc(sizeof(xt_generator), XT_OBJECT_KIND_GENERATOR);
  gen->function = function;
  gen->this_value = thisValue;
  gen->argc = argc;
  gen->args = NULL;
  gen->coroutine = NULL;
  gen->result = XT_UNDEFINED;
  gen->sent = XT_UNDEFINED;
  gen->iter_value = XT_UNDEFINED;
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
    xt_throw(xt_string_from_cstr("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
  if (gen->done) return xt_generator_result_object(XT_UNDEFINED, 1);
  gen->sent = sent;
  gen->resume_kind = 0;
  xt_generator_resume(gen);
  if (gen->done) {
    if (gen->threw) xt_throw(gen->result);
    return xt_generator_result_object(gen->result, 1);
  }
  return xt_generator_result_object(gen->result, 0);
}

xt_value xt_generator_throw(xt_value generator, xt_value error) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen) {
    xt_throw(xt_string_from_cstr("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
  if (gen->done) {
    xt_throw(error);
    return XT_UNDEFINED;
  }
  gen->sent = error;
  gen->resume_kind = 1;
  xt_generator_resume(gen);
  if (gen->done) {
    if (gen->threw) xt_throw(gen->result);
    return xt_generator_result_object(gen->result, 1);
  }
  return xt_generator_result_object(gen->result, 0);
}

xt_value xt_generator_return(xt_value generator, xt_value value) {
  xt_generator *gen = xt_as_generator(generator);
  if (!gen) {
    xt_throw(xt_string_from_cstr("TypeError: not a generator"));
    return XT_UNDEFINED;
  }
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
  xt_coroutine_leave(gen->coroutine);
  if (gen->resume_kind == 1) {
    xt_throw(gen->sent);
    return XT_UNDEFINED;
  }
  if (gen->resume_kind == 2) {
    gen->done = 1;
    gen->threw = 0;
    gen->result = gen->sent;
    xt_coroutine_leave(gen->coroutine);
    return XT_UNDEFINED;
  }
  return gen->sent;
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
  if (!gen || gen->done) return 0;
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
