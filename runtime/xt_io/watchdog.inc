/* ------------------------------------------------------------------------- */
/* Hang watchdog                                                             */
/* ------------------------------------------------------------------------- */

/*
 * A compiled program that spins — an unexpected infinite loop in user code, a
 * blocking host call, a pathological input — prints nothing at all, and a GUI
 * program merely stops painting. There is no debugger attached and no V8 to
 * request a profile from, so "it hangs" is all the user can report.
 *
 * Setting `XT_STACK_WATCHDOG_MS=<n>` starts a sampler thread that watches the
 * shadow call stack: when no function has been entered or left for `n`
 * milliseconds, it prints the innermost frames (name, file, line). That turns a
 * silent hang into the source location that is spinning.
 *
 * Cost when the variable is unset: one relaxed counter increment per compiled
 * call and nothing else — the thread is not started. The frame array is read
 * from another thread without a lock, which can tear a frame in the middle of
 * an update; that is acceptable for a diagnostic that only runs when asked for.
 * The stack data itself lives in `exception.inc`.
 */

#if !defined(_WIN32)
#include <pthread.h>
#endif

static volatile int g_watchdog_started = 0;
static volatile int g_watchdog_stop = 0;

/** Monotonic milliseconds. */
static unsigned long long xt_watchdog_now_ms(void) {
#if defined(_WIN32)
  return (unsigned long long)GetTickCount64();
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)(ts.tv_nsec / 1000000);
#endif
}

#if defined(_WIN32)
static unsigned __stdcall xt_watchdog_entry(void *argument) {
#else
static void *xt_watchdog_entry(void *argument) {
#endif
  unsigned long long threshold = (unsigned long long)(uintptr_t)argument;
  unsigned long long interval = threshold / 4;
  if (interval < 20) interval = 20;
  unsigned long long last_ticks = g_stack_ticks;
  unsigned long long idle_since = xt_watchdog_now_ms();
  int sample_all = getenv("XT_STACK_SAMPLE_ALL") != NULL;
  while (!g_watchdog_stop) {
    unsigned long long now;
#if defined(_WIN32)
    Sleep((DWORD)interval);
#else
    struct timespec sleep_for;
    sleep_for.tv_sec = (time_t)(interval / 1000);
    sleep_for.tv_nsec = (long)((interval % 1000) * 1000000);
    nanosleep(&sleep_for, NULL);
#endif
    if (g_watchdog_stop) break;
    now = xt_watchdog_now_ms();
    if (!sample_all && g_stack_ticks != last_ticks) {
      last_ticks = g_stack_ticks;
      idle_since = now;
      continue;
    }
    if (!sample_all && now - idle_since < threshold) continue;
    if (sample_all) last_ticks = g_stack_ticks;
    fprintf(stderr, "xt: no JavaScript call for %llu ms, shadow stack:\n", now - idle_since);
    xt_stack_dump_frames();
    fflush(stderr);
    /* Report once per idle period, not once per interval. */
    idle_since = now;
  }
#if defined(_WIN32)
  return 0;
#else
  return NULL;
#endif
}

/** Start the sampler once, if `XT_STACK_WATCHDOG_MS` asks for it. */
static void xt_stack_watchdog_start_once(void) {
  if (g_watchdog_started) return;
  g_watchdog_started = 1;
  const char *configured = getenv("XT_STACK_WATCHDOG_MS");
  if (configured == NULL || configured[0] == 0) return;
  long ms = strtol(configured, NULL, 10);
  if (ms <= 0) return;
#if defined(_WIN32)
  uintptr_t handle = _beginthreadex(NULL, 0, xt_watchdog_entry, (void *)(uintptr_t)ms, 0, NULL);
  if (handle != 0) CloseHandle((HANDLE)handle);
#else
  pthread_t thread;
  if (pthread_create(&thread, NULL, xt_watchdog_entry, (void *)(uintptr_t)ms) == 0) {
    pthread_detach(thread);
  }
#endif
}
