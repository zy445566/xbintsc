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
 * POSIX uses `ucontext` (`swapcontext`); Windows uses fibers. The generator's
 * exception stack is swapped alongside it so a `throw` inside the body still
 * targets the body's own `try` frames.
 */

#define _XOPEN_SOURCE 700

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif

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
  int booted;
  void *saved_try_top;
#ifdef _WIN32
  void *fiber;
  void *caller_fiber;
#else
  ucontext_t ctx;
  ucontext_t caller;
  char *stack;
#endif
} xt_generator;

/* The generator whose coroutine is running, if any. */
static xt_generator *g_active_generator = NULL;
/* Filled in just before a coroutine is first entered (POSIX has no portable
 * way to pass a pointer to `makecontext`). */
static xt_generator *g_boot_generator = NULL;
#ifdef _WIN32
static void *g_main_fiber = NULL;
#endif

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

static void xt_generator_suspend(xt_generator *gen) {
  gen->saved_try_top = xt_try_mark();
#ifdef _WIN32
  SwitchToFiber(gen->caller_fiber);
#else
  swapcontext(&gen->ctx, &gen->caller);
#endif
}

/* Runs on the generator's private stack. */
static void xt_generator_run(xt_generator *gen) {
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
  xt_generator_suspend(gen);
}

#ifndef _WIN32
static void xt_generator_entry(void) {
  xt_generator *gen = g_boot_generator;
  g_boot_generator = NULL;
  xt_generator_run(gen);
}
#else
static void CALLBACK xt_generator_entry(void *param) {
  xt_generator_run((xt_generator *)param);
}
#endif

static void xt_generator_boot(xt_generator *gen) {
#ifdef _WIN32
  if (!g_main_fiber) g_main_fiber = ConvertThreadToFiber(NULL);
  gen->fiber = CreateFiber(1 << 20, xt_generator_entry, gen);
  if (!gen->fiber) abort();
#else
  size_t size = 1 << 20;
  gen->stack = (char *)malloc(size);
  if (!gen->stack) abort();
  if (getcontext(&gen->ctx) != 0) abort();
  gen->ctx.uc_stack.ss_sp = gen->stack;
  gen->ctx.uc_stack.ss_size = size;
  gen->ctx.uc_link = NULL;
  g_boot_generator = gen;
  makecontext(&gen->ctx, xt_generator_entry, 0);
#endif
  gen->booted = 1;
}

/* Release the private stack once the coroutine has finished. The generator
 * object itself stays valid; later `.next()` calls just report `done`. */
static void xt_generator_dispose(xt_generator *gen) {
#ifdef _WIN32
  if (gen->fiber) {
    DeleteFiber(gen->fiber);
    gen->fiber = NULL;
  }
  gen->caller_fiber = NULL;
#else
  if (gen->stack) {
    free(gen->stack);
    gen->stack = NULL;
  }
#endif
}

static void xt_generator_resume(xt_generator *gen) {
  if (!gen->booted) xt_generator_boot(gen);
  xt_generator *previous = g_active_generator;
  g_active_generator = gen;
  void *caller_try = xt_try_mark();
  xt_try_restore(gen->saved_try_top);
#ifdef _WIN32
  gen->caller_fiber = GetCurrentFiber();
  SwitchToFiber(gen->fiber);
#else
  swapcontext(&gen->caller, &gen->ctx);
#endif
  gen->saved_try_top = xt_try_mark();
  xt_try_restore(caller_try);
  if (gen->done) xt_generator_dispose(gen);
  g_active_generator = previous;
}

xt_value xt_generator_new(xt_value function, xt_value thisValue, int32_t argc, xt_value *argv) {
  xt_generator *gen = (xt_generator *)xt_alloc(sizeof(xt_generator), XT_OBJECT_KIND_GENERATOR);
  gen->function = function;
  gen->this_value = thisValue;
  gen->argc = argc;
  gen->result = XT_UNDEFINED;
  gen->sent = XT_UNDEFINED;
  gen->iter_value = XT_UNDEFINED;
  if (argc > 0 && argv) {
    gen->args = (xt_value *)malloc(sizeof(xt_value) * (size_t)argc);
    if (!gen->args) abort();
    memcpy(gen->args, argv, sizeof(xt_value) * (size_t)argc);
  }
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
  xt_generator_suspend(gen);
  if (gen->resume_kind == 1) {
    xt_throw(gen->sent);
    return XT_UNDEFINED;
  }
  if (gen->resume_kind == 2) {
    gen->done = 1;
    gen->threw = 0;
    gen->result = gen->sent;
    xt_generator_suspend(gen);
    return XT_UNDEFINED;
  }
  return gen->sent;
}

/* `yield* delegate`: relay every value the delegate produces. */
xt_value xt_yield_star(xt_value delegate) {
  xt_value nextName = xt_string_from_cstr("next");
  int usesNext = XT_IS_FUNCTION(xt_get(delegate, nextName));
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
