/*
 * xbintsc runtime — opt-in call counters for performance work.
 *
 * `XT_PROFILE=1` increments one counter per entry of the runtime's hottest
 * helpers and prints the totals at exit. A counter is one branch and one
 * increment, so a profiled run keeps the shape of the workload; pair the counts
 * with a microbenchmark of the same helper to attribute time.
 *
 * This is diagnostics, not a feature: the counters are off unless the variable
 * is set, and the dump goes to stderr.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>

int xt_profile_on = 0;
unsigned long long xt_profile_calls[XT_PROFILE_COUNT];

static const char *const xt_profile_names[XT_PROFILE_COUNT] = {
    "alloc",
    "gc",
    "math",
    "set",
    "get-ic",
    "method-ic",
    "object-set",
    "object-insert",
    "object-find",
    "object-reserve",
    "closure-call",
    "array-get",
    "string-cstr",
    "typed-read",
    "typed-write",
};

static void xt_profile_dump(void) {
  if (!xt_profile_on) return;
  for (int i = 0; i < XT_PROFILE_COUNT; i++) {
    if (xt_profile_calls[i] == 0) continue;
    fprintf(stderr, "xt_profile: %-14s %llu\n", xt_profile_names[i], xt_profile_calls[i]);
  }
}

void xt_profile_init(void) {
  const char *env = getenv("XT_PROFILE");
  if (env == NULL || env[0] == '\0' || env[0] == '0') return;
  xt_profile_on = 1;
  atexit(xt_profile_dump);
}
