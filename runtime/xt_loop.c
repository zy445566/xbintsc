/*
 * xbintsc runtime — host event loop.
 *
 * A tiny `select(2)` based reactor that lets host extensions (node's `net`,
 * `dgram`, `http`, ...) deliver callbacks without baking an OS abstraction
 * into the compiler. Generated `main` calls `xt_run_event_loop()` once the
 * program body (and its microtasks) have finished; the loop returns as soon as
 * no file descriptors or timers remain registered, so programs that never touch
 * I/O or timers are unaffected.
 *
 * Extensions register descriptors with `xt_loop_add` and receive readiness
 * callbacks. The timer globals (`setTimeout` / `setInterval`) share the same
 * wait: the nearest deadline bounds `select(2)`, so a timer-only program sleeps
 * instead of spinning. The loop drains the promise microtask queue after every
 * batch of callbacks so `await`/`.then` chains keep working inside handlers.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define XT_LOOP_MAX_WATCHERS 1024

typedef struct {
  int fd;
  int events;
  xt_io_handler handler;
  void *userdata;
  int active;
} xt_loop_watcher;

static xt_loop_watcher xt_loop_watchers[XT_LOOP_MAX_WATCHERS];
static int xt_loop_watcher_count = 0;
static int xt_loop_active_count = 0;

static xt_loop_watcher *xt_loop_find(int fd) {
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    if (xt_loop_watchers[i].active && xt_loop_watchers[i].fd == fd) return &xt_loop_watchers[i];
  }
  return NULL;
}

/* -- timers --------------------------------------------------------------- */
/*
 * `setTimeout` / `setInterval` and their `clear*` counterparts are implemented
 * here, next to the reactor they piggyback on. The pending timers live in a
 * flat array (the same shape as the watcher table above); before every wait the
 * reactor looks at the nearest deadline and uses it to bound `select(2)`, so a
 * program whose only pending work is a timer still sleeps instead of spinning.
 *
 * Callbacks are ordinary closure values invoked through `xt_closure_call`, so
 * they capture their lexical environment like any other JS function.
 */
typedef struct {
  uint64_t id;
  xt_value callback;
  xt_value *args;
  int32_t argc;
  int64_t due_ms;
  int64_t interval_ms;
  int active;
} xt_timer;

static xt_timer *xt_timers = NULL;
static uint32_t xt_timer_count = 0;
static uint32_t xt_timer_capacity = 0;
static uint64_t xt_timer_next_id = 1;

static int64_t xt_loop_now_ms(void) {
#if defined(_WIN32)
  return (int64_t)GetTickCount64();
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
#endif
}

static void xt_loop_sleep_ms(int ms) {
  if (ms <= 0) return;
#if defined(_WIN32)
  Sleep((DWORD)ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

/* Coerce a delay argument to a non-negative whole number of milliseconds. */
static int64_t xt_timer_delay_arg(xt_value value) {
  double d = xt_to_number(value);
  if (d != d || d < 0) return 0;
  if (d > 2147483647.0) return 2147483647;
  return (int64_t)d;
}

static uint32_t xt_timer_active_count(void) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < xt_timer_count; i++) {
    if (xt_timers[i].active) count++;
  }
  return count;
}

/* Earliest deadline among the live timers, or -1 when none is pending. */
static int64_t xt_timer_next_due(void) {
  int64_t next = -1;
  for (uint32_t i = 0; i < xt_timer_count; i++) {
    if (!xt_timers[i].active) continue;
    if (next < 0 || xt_timers[i].due_ms < next) next = xt_timers[i].due_ms;
  }
  return next;
}

/* Reclaim slots left by cleared/fired one-shot timers. */
static void xt_timer_compact(void) {
  uint32_t write = 0;
  for (uint32_t read = 0; read < xt_timer_count; read++) {
    if (!xt_timers[read].active) {
      free(xt_timers[read].args);
      xt_timers[read].args = NULL;
      xt_timers[read].argc = 0;
      continue;
    }
    if (write != read) xt_timers[write] = xt_timers[read];
    write++;
  }
  xt_timer_count = write;
}

static void xt_loop_gc_register(void);

static uint64_t xt_timer_add(xt_value callback, int64_t delay_ms, int64_t interval_ms, int32_t argc, xt_value *argv) {
  xt_loop_gc_register();
  if (xt_timer_count >= xt_timer_capacity) {
    xt_timer_capacity = xt_timer_capacity == 0 ? 16 : xt_timer_capacity * 2;
    xt_timer *grown = (xt_timer *)realloc(xt_timers, sizeof(xt_timer) * xt_timer_capacity);
    if (!grown) abort();
    xt_timers = grown;
  }
  xt_timer *timer = &xt_timers[xt_timer_count++];
  timer->id = xt_timer_next_id++;
  timer->callback = callback;
  timer->argc = argc;
  timer->args = NULL;
  if (argc > 0) {
    timer->args = (xt_value *)malloc(sizeof(xt_value) * (size_t)argc);
    if (!timer->args) abort();
    for (int32_t i = 0; i < argc; i++) timer->args[i] = argv[i];
  }
  timer->due_ms = xt_loop_now_ms() + delay_ms;
  timer->interval_ms = interval_ms;
  timer->active = 1;
  return timer->id;
}

static int xt_timer_cancel(uint64_t id) {
  for (uint32_t i = 0; i < xt_timer_count; i++) {
    if (xt_timers[i].active && xt_timers[i].id == id) {
      xt_timers[i].active = 0;
      return 1;
    }
  }
  return 0;
}

/* Watchers and pending timers hold values outside the value graph: the timer
 * callbacks/arguments live in the flat array, and extension watchers keep an
 * `xt_value *` box in their opaque userdata (the `xt_loop_add` convention). */
static void xt_loop_gc_scan_roots(void) {
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    if (!xt_loop_watchers[i].active) continue;
    xt_value *boxed = (xt_value *)xt_loop_watchers[i].userdata;
    if (boxed) xt_gc_mark_value(*boxed);
  }
  for (uint32_t i = 0; i < xt_timer_count; i++) {
    if (!xt_timers[i].active) continue;
    xt_gc_mark_value(xt_timers[i].callback);
    if (xt_timers[i].args && xt_timers[i].argc > 0) {
      xt_gc_scan_region(xt_timers[i].args, sizeof(xt_value) * (size_t)xt_timers[i].argc);
    }
  }
}

static void xt_loop_gc_register(void) {
  static int registered = 0;
  if (registered) return;
  registered = 1;
  xt_gc_register_root_provider(xt_loop_gc_scan_roots);
}

/* Invoke every timer whose deadline has passed. Timers scheduled by a callback
 * (or by a microtask) are left for the next reactor iteration, so a recursive
 * `setTimeout(fn, 0)` cannot spin forever inside a single call. */
static void xt_timer_run_due(void) {
  xt_timer_compact();
  int64_t now = xt_loop_now_ms();
  uint32_t limit = xt_timer_count;
  for (uint32_t i = 0; i < limit; i++) {
    xt_timer *timer = &xt_timers[i];
    if (!timer->active || timer->due_ms > now) continue;
    xt_value callback = timer->callback;
    int32_t argc = timer->argc;
    xt_value *args = timer->args;
    if (timer->interval_ms > 0) {
      timer->due_ms = now + timer->interval_ms;
    } else {
      timer->active = 0;
    }
    if (XT_IS_FUNCTION(callback)) xt_closure_call(callback, argc, args);
  }
}

xt_value xt_set_timeout(int32_t argc, xt_value *argv) {
  xt_value callback = xt_arg_at(argc, argv, 0);
  if (!XT_IS_FUNCTION(callback)) {
    xt_throw(xt_string_from_cstr("TypeError: The \"callback\" argument must be of type function"));
    return XT_UNDEFINED;
  }
  int64_t delay = xt_timer_delay_arg(xt_arg_at(argc, argv, 1));
  int32_t extra = argc > 2 ? argc - 2 : 0;
  uint64_t id = xt_timer_add(callback, delay, 0, extra, extra > 0 ? argv + 2 : NULL);
  return xt_number((double)id);
}

xt_value xt_set_interval(int32_t argc, xt_value *argv) {
  xt_value callback = xt_arg_at(argc, argv, 0);
  if (!XT_IS_FUNCTION(callback)) {
    xt_throw(xt_string_from_cstr("TypeError: The \"callback\" argument must be of type function"));
    return XT_UNDEFINED;
  }
  int64_t delay = xt_timer_delay_arg(xt_arg_at(argc, argv, 1));
  /* Intervals are clamped to at least 1ms so an interval can never busy-spin. */
  int64_t interval = delay > 0 ? delay : 1;
  int32_t extra = argc > 2 ? argc - 2 : 0;
  uint64_t id = xt_timer_add(callback, delay, interval, extra, extra > 0 ? argv + 2 : NULL);
  return xt_number((double)id);
}

xt_value xt_clear_timeout(int32_t argc, xt_value *argv) {
  double id = xt_to_number(xt_arg_at(argc, argv, 0));
  if (id >= 1) xt_timer_cancel((uint64_t)id);
  return XT_UNDEFINED;
}

xt_value xt_clear_interval(int32_t argc, xt_value *argv) {
  double id = xt_to_number(xt_arg_at(argc, argv, 0));
  if (id >= 1) xt_timer_cancel((uint64_t)id);
  return XT_UNDEFINED;
}

int xt_loop_add(int fd, int events, xt_io_handler handler, void *userdata) {
  if (fd < 0 || !handler) return -1;
  xt_loop_gc_register();
  xt_loop_watcher *existing = xt_loop_find(fd);
  if (existing) {
    existing->events = events;
    existing->handler = handler;
    existing->userdata = userdata;
    return 0;
  }
  if (xt_loop_watcher_count >= XT_LOOP_MAX_WATCHERS) return -1;
  xt_loop_watcher *watcher = &xt_loop_watchers[xt_loop_watcher_count++];
  watcher->fd = fd;
  watcher->events = events;
  watcher->handler = handler;
  watcher->userdata = userdata;
  watcher->active = 1;
  xt_loop_active_count++;
  return 0;
}

void xt_loop_update(int fd, int events) {
  xt_loop_watcher *watcher = xt_loop_find(fd);
  if (watcher) watcher->events = events;
}

void xt_loop_remove(int fd) {
  xt_loop_watcher *watcher = xt_loop_find(fd);
  if (!watcher) return;
  watcher->active = 0;
  watcher->fd = -1;
  xt_loop_active_count--;
}

/* A host (GUI toolkit, embedder) can take over the main loop; see
 * `xt_loop_set_main`. `NULL` keeps the default blocking behaviour. */
static xt_main_loop_fn xt_loop_main = NULL;

void xt_loop_set_main(xt_main_loop_fn fn) { xt_loop_main = fn; }

int xt_loop_poll(int timeout_ms) {
  int has_fds = xt_loop_active_count > 0;
  int64_t next_due = xt_timer_next_due();
  if (!has_fds && next_due < 0) {
    xt_drain_microtasks();
    return 0;
  }

  /* Bound the wait by the nearest timer deadline when one is pending. */
  int wait_ms = timeout_ms;
  if (next_due >= 0) {
    int64_t delta = next_due - xt_loop_now_ms();
    if (delta < 0) delta = 0;
    if (delta > 100000000) delta = 100000000;
    if (wait_ms < 0 || delta < (int64_t)wait_ms) wait_ms = (int)delta;
  }

  /* Timers only: sleep, then run whatever is due. */
  if (!has_fds) {
    xt_loop_sleep_ms(wait_ms);
    xt_timer_run_due();
    xt_drain_microtasks();
    return (int)(xt_loop_active_count + xt_timer_active_count());
  }

  fd_set readSet;
  fd_set writeSet;
  FD_ZERO(&readSet);
  FD_ZERO(&writeSet);
  int maxFd = -1;
  for (int i = 0; i < xt_loop_watcher_count; i++) {
    xt_loop_watcher *watcher = &xt_loop_watchers[i];
    if (!watcher->active) continue;
    if (watcher->events & XT_IO_READ) FD_SET(watcher->fd, &readSet);
    if (watcher->events & XT_IO_WRITE) FD_SET(watcher->fd, &writeSet);
    if (watcher->fd > maxFd) maxFd = watcher->fd;
  }
  if (maxFd < 0) {
    xt_timer_run_due();
    xt_drain_microtasks();
    return (int)(xt_loop_active_count + xt_timer_active_count());
  }

  struct timeval timeout;
  struct timeval *timeoutPtr = NULL;
  if (wait_ms >= 0) {
    timeout.tv_sec = wait_ms / 1000;
    timeout.tv_usec = (wait_ms % 1000) * 1000;
    timeoutPtr = &timeout;
  }

  int ready = select(maxFd + 1, &readSet, &writeSet, NULL, timeoutPtr);
  if (ready < 0) return -1;

  if (ready > 0) {
    /* Collect the ready watchers first: handlers may add/remove descriptors. */
    for (int i = 0; i < xt_loop_watcher_count; i++) {
      xt_loop_watcher *watcher = &xt_loop_watchers[i];
      if (!watcher->active) continue;
      int events = 0;
      if ((watcher->events & XT_IO_READ) && FD_ISSET(watcher->fd, &readSet)) events |= XT_IO_READ;
      if ((watcher->events & XT_IO_WRITE) && FD_ISSET(watcher->fd, &writeSet)) events |= XT_IO_WRITE;
      if (events) watcher->handler(watcher->userdata, events);
    }
  }

  xt_timer_run_due();
  xt_drain_microtasks();
  return (int)(xt_loop_active_count + xt_timer_active_count());
}

void xt_run_event_loop(void) {
  /* A host that owns the main loop (GUI toolkit) takes precedence: it is
     responsible for calling `xt_loop_poll` while it runs. */
  if (xt_loop_main != NULL) {
    xt_loop_main();
    return;
  }
  while (xt_loop_has_work()) {
    if (xt_loop_poll(-1) < 0) break;
  }
}

int xt_loop_has_work(void) { return xt_loop_active_count > 0 || xt_timer_active_count() > 0; }
