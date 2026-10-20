/*
 * xbintsc runtime — stackful coroutines.
 *
 * A coroutine owns a private C stack and switches to it with `ucontext`
 * (POSIX) or fibers (Windows). Because the body keeps a real call stack,
 * `try`/`finally`, nested loops and ordinary locals work across suspension
 * points without a state-machine transform. Generators (`xt_generator.c`) and
 * `async` functions (`xt_async.c`) are both built on this module.
 *
 * The coroutine's exception stack is swapped alongside its C stack, so a
 * `throw` inside the body still targets the body's own `try` frames. Its stack
 * is registered as an active GC region while it runs and scanned while it is
 * suspended (through the owner's trace hook).
 *
 * The owner object decides *when* to enter/leave; this module only performs
 * the switch, the exception-stack hand-off and the GC region bookkeeping.
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

/* Generous enough for deep JavaScript recursion on top of a coroutine. The
 * pages are committed lazily by the OS, so a suspended coroutine is cheap. */
#define XT_COROUTINE_STACK_SIZE ((size_t)1 << 20)

struct xt_coroutine {
  void (*entry)(void *);
  void *arg;
  /* Private stack bounds, registered as an active stack region while the
   * coroutine runs and scanned conservatively while it is suspended. */
  char *stack;
  size_t stack_size;
  int stack_pushed;
  int booted;
  int started;
  void *saved_try_top;
#ifdef _WIN32
  void *fiber;
  void *caller_fiber;
  /* `SwitchToFiber` keeps the resumer's callee-saved registers in an opaque
   * fiber context (POSIX stores them in `caller`), so snapshot them for the
   * collector before switching away. */
  jmp_buf caller_regs;
  int has_caller_regs;
#else
  ucontext_t ctx;
  ucontext_t caller;
#endif
};

/* Filled in just before a coroutine is first entered (POSIX has no portable
 * way to pass a pointer to `makecontext`). */
static xt_coroutine *g_boot_coroutine = NULL;
#ifdef _WIN32
static void *g_main_fiber = NULL;
#else
/* Async functions are frequently short-lived, so keep a few private stacks
 * around instead of asking the OS for a fresh 1 MiB mapping every call. The
 * cached stacks hold no live values and are not part of the GC scan. */
#define XT_COROUTINE_STACK_CACHE 8
static char *g_stack_cache[XT_COROUTINE_STACK_CACHE];
static int g_stack_cache_count = 0;
#endif

xt_coroutine *xt_coroutine_create(void (*entry)(void *), void *arg) {
  xt_coroutine *co = (xt_coroutine *)calloc(1, sizeof(xt_coroutine));
  if (!co) abort();
  co->entry = entry;
  co->arg = arg;
  return co;
}

#ifndef _WIN32
static void xt_coroutine_entry(void) {
  xt_coroutine *co = g_boot_coroutine;
  g_boot_coroutine = NULL;
  co->entry(co->arg);
}
#else
static void CALLBACK xt_coroutine_entry(void *param) {
  xt_coroutine *co = (xt_coroutine *)param;
  /* The fiber stack is not observable from the outside; capture its bounds
   * here (before the body allocates anything) so a collection during the body
   * has a valid scan region. */
  MEMORY_BASIC_INFORMATION info;
  uintptr_t here = (uintptr_t)&info;
  if (VirtualQuery((LPCVOID)here, &info, sizeof(info))) {
    /* Use the *committed* region containing the current frame: a Windows
     * stack reserves more than it commits, and `AllocationBase + RegionSize`
     * can fall in uncommitted memory. `BaseAddress + RegionSize` is the
     * stack top. */
    co->stack = (char *)info.BaseAddress;
    co->stack_size = info.RegionSize;
  }
  if (co->stack && co->stack_size && !co->stack_pushed) {
    xt_gc_push_stack((uintptr_t)co->stack, (uintptr_t)co->stack + co->stack_size);
    co->stack_pushed = 1;
  }
  co->entry(co->arg);
}
#endif

static void xt_coroutine_boot(xt_coroutine *co) {
#ifdef _WIN32
  if (!g_main_fiber) g_main_fiber = ConvertThreadToFiber(NULL);
  co->fiber = CreateFiber(XT_COROUTINE_STACK_SIZE, xt_coroutine_entry, co);
  if (!co->fiber) abort();
#else
  size_t size = XT_COROUTINE_STACK_SIZE;
  char *stack = NULL;
  if (g_stack_cache_count > 0) stack = g_stack_cache[--g_stack_cache_count];
  if (!stack) stack = (char *)malloc(size);
  if (!stack) abort();
  co->stack = stack;
  co->stack_size = size;
  if (getcontext(&co->ctx) != 0) abort();
  co->ctx.uc_stack.ss_sp = co->stack;
  co->ctx.uc_stack.ss_size = size;
  co->ctx.uc_link = NULL;
  g_boot_coroutine = co;
  makecontext(&co->ctx, xt_coroutine_entry, 0);
#endif
  co->booted = 1;
}

void xt_coroutine_enter(xt_coroutine *co) {
  if (!co->booted) xt_coroutine_boot(co);
  /* Record where the current (calling) stack suspends before switching away.
   * On Windows the first push happens inside the fiber once its stack bounds
   * are known, so the caller's region must be marked suspended here, on the
   * caller's own stack. */
  xt_gc_suspend((uintptr_t)&co);
  if (co->stack && co->stack_size && !co->stack_pushed) {
    xt_gc_push_stack((uintptr_t)co->stack, (uintptr_t)co->stack + co->stack_size);
    co->stack_pushed = 1;
  }
  void *caller_try = xt_try_mark();
  xt_try_restore(co->saved_try_top);
#ifdef _WIN32
  _setjmp(co->caller_regs);
  co->has_caller_regs = 1;
  co->caller_fiber = GetCurrentFiber();
  SwitchToFiber(co->fiber);
#else
  swapcontext(&co->caller, &co->ctx);
#endif
  co->saved_try_top = xt_try_mark();
  xt_try_restore(caller_try);
  if (co->stack_pushed) {
    xt_gc_pop_stack();
    co->stack_pushed = 0;
  }
}

void xt_coroutine_leave(xt_coroutine *co) {
  co->saved_try_top = xt_try_mark();
#ifdef _WIN32
  SwitchToFiber(co->caller_fiber);
#else
  swapcontext(&co->ctx, &co->caller);
#endif
}

void xt_coroutine_gc_trace(xt_coroutine *co) {
  if (!co) return;
  if (co->stack && co->stack_size) xt_gc_scan_region(co->stack, co->stack_size);
#ifndef _WIN32
  /* Registers saved by `swapcontext` at the last suspension are not on the
   * stack. */
  xt_gc_scan_region(&co->ctx, sizeof(co->ctx));
#else
  if (co->has_caller_regs) xt_gc_scan_region(&co->caller_regs, sizeof(co->caller_regs));
#endif
}

void xt_coroutine_release(xt_coroutine *co) {
  if (!co) return;
#ifdef _WIN32
  if (co->fiber) {
    DeleteFiber(co->fiber);
    co->fiber = NULL;
  }
  co->caller_fiber = NULL;
  /* `DeleteFiber` releases the stack: never scan it again. */
  co->stack = NULL;
  co->stack_size = 0;
#else
  if (co->stack && co->stack_size == XT_COROUTINE_STACK_SIZE &&
      g_stack_cache_count < XT_COROUTINE_STACK_CACHE) {
    g_stack_cache[g_stack_cache_count++] = co->stack;
  } else {
    free(co->stack);
  }
  co->stack = NULL;
  co->stack_size = 0;
#endif
  free(co);
}
