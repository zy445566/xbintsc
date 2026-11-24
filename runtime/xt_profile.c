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
#include <string.h>

#define XT_PROFILE_KEY_SLOTS 512
#define XT_PROFILE_KEY_PROBES 8

typedef struct {
  char *key;
  unsigned long long count;
} xt_profile_key;

static xt_profile_key g_keys[XT_PROFILE_KEY_SLOTS];
static unsigned long long g_alloc_kinds[32];

int xt_profile_on = 0;
unsigned long long xt_profile_calls[XT_PROFILE_COUNT];

void xt_profile_note_alloc(int kind) {
  if (!xt_profile_on) return;
  if (kind >= 0 && kind < 32) g_alloc_kinds[kind]++;
}

void xt_profile_note_cstr(const char *key) {
  if (!xt_profile_on || key == NULL) return;
  uint64_t hash = 1469598103934665603ull;
  for (const char *p = key; *p != '\0'; p++) {
    hash ^= (unsigned char)*p;
    hash *= 1099511628211ull;
  }
  const size_t start = (size_t)(hash & (XT_PROFILE_KEY_SLOTS - 1));
  for (size_t probe = 0; probe < XT_PROFILE_KEY_PROBES; probe++) {
    xt_profile_key *slot = &g_keys[(start + probe) & (XT_PROFILE_KEY_SLOTS - 1)];
    if (slot->key == NULL) {
      const size_t length = strlen(key) + 1;
      char *copy = (char *)malloc(length);
      if (copy == NULL) return;
      memcpy(copy, key, length);
      slot->key = copy;
      slot->count = 1;
      return;
    }
    if (strcmp(slot->key, key) == 0) {
      slot->count++;
      return;
    }
  }
}

static const char *const xt_profile_kind_names[32] = {
    "none",       "string",     "object",       "array",        "function",
    "promise",    "map",        "set",          "date",         "regexp",
    "symbol",     "bigint",     "error",        "generator",    "iterator",
    "async",      "async-iter", "weak-map",     "weak-set",     "weak-ref",
    "array-buffer",
};

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
    "ic-hit",
    "ic-early",
    "ic-miss",
};

static void xt_profile_dump(void) {
  if (!xt_profile_on) return;
  for (int i = 0; i < XT_PROFILE_COUNT; i++) {
    if (xt_profile_calls[i] == 0) continue;
    fprintf(stderr, "xt_profile: %-14s %llu\n", xt_profile_names[i], xt_profile_calls[i]);
  }
  for (int kind = 0; kind < 32; kind++) {
    if (g_alloc_kinds[kind] == 0) continue;
    const char *name = kind < (int)(sizeof(xt_profile_kind_names) / sizeof(char *))
                           ? xt_profile_kind_names[kind]
                           : "other";
    fprintf(stderr, "xt_profile: alloc %-12s %llu\n", name, g_alloc_kinds[kind]);
  }
  /* The twenty most frequent C-string property names: each was an allocated
   * xt_string unless the runtime interns it. */
  for (int rank = 0; rank < 20; rank++) {
    long best = -1;
    unsigned long long best_count = 0;
    for (int i = 0; i < XT_PROFILE_KEY_SLOTS; i++) {
      if (g_keys[i].key == NULL || g_keys[i].count == 0) continue;
      if (g_keys[i].count > best_count) {
        best_count = g_keys[i].count;
        best = i;
      }
    }
    if (best < 0) break;
    fprintf(stderr, "xt_profile: key %-24s %llu\n", g_keys[best].key, g_keys[best].count);
    g_keys[best].count = 0;
  }
}

void xt_profile_init(void) {
  const char *env = getenv("XT_PROFILE");
  if (env == NULL || env[0] == '\0' || env[0] == '0') return;
  xt_profile_on = 1;
  atexit(xt_profile_dump);
}
