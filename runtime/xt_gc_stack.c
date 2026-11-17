/*
 * xbintsc runtime — GC stack-region table.
 *
 * A coroutine has a private C stack; while it runs (or while it is suspended
 * under a deeper coroutine) the conservative collector must scan that region.
 * The table grows on demand: an `async` call chain can be deeper than any fixed
 * limit, and silently dropping a push would desynchronise push/pop, so a later
 * pop evicted a still-live parent region and the collector stopped scanning it.
 */

#include "rt_internal.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uintptr_t low;
  uintptr_t high;
  /* Lower bound captured from the stack below this region when a deeper region
   * was entered on top of it. `UINTPTR_MAX` means "this is the top region"; the
   * collector then scans from the current stack pointer. */
  uintptr_t scan_low;
} xt_stack_region;

static xt_stack_region *g_stack_regions = NULL;
static size_t g_stack_depth = 0;
static size_t g_stack_capacity = 0;

/** Ensure room for one more region; aborts when the process is out of memory. */
static void xt_gc_reserve_stack_region(void) {
  if (g_stack_depth < g_stack_capacity) return;
  size_t capacity = g_stack_capacity == 0 ? 16 : g_stack_capacity * 2;
  xt_stack_region *grown = (xt_stack_region *)realloc(g_stack_regions, sizeof(xt_stack_region) * capacity);
  if (!grown) {
    fprintf(stderr, "xbintsc: out of memory growing the GC stack-region table\n");
    abort();
  }
  g_stack_regions = grown;
  g_stack_capacity = capacity;
}

void xt_gc_init_main_stack(uintptr_t high) {
  xt_gc_reserve_stack_region();
  if (g_stack_depth == 0) {
    g_stack_regions[0].low = 0;
    g_stack_regions[0].high = high;
    g_stack_regions[0].scan_low = UINTPTR_MAX;
    g_stack_depth = 1;
  } else if (high > g_stack_regions[0].high) {
    g_stack_regions[0].high = high;
  }
}

void xt_gc_push_stack(uintptr_t low, uintptr_t high) {
  xt_gc_reserve_stack_region();
  g_stack_regions[g_stack_depth].low = low;
  g_stack_regions[g_stack_depth].high = high;
  g_stack_regions[g_stack_depth].scan_low = low;
  g_stack_depth++;
}

void xt_gc_suspend(uintptr_t sp) {
  if (g_stack_depth == 0) return;
  uintptr_t *scan = &g_stack_regions[g_stack_depth - 1].scan_low;
  if (*scan == UINTPTR_MAX || sp < *scan) *scan = sp;
}

void xt_gc_pop_stack(void) {
  if (g_stack_depth <= 1) return;
  g_stack_depth--;
  /* Back on the underlying stack: resume scanning it from the current frame. */
  if (g_stack_depth > 0) g_stack_regions[g_stack_depth - 1].scan_low = UINTPTR_MAX;
}

size_t xt_gc_stack_region_count(void) {
  return g_stack_depth;
}

void xt_gc_stack_region_bounds(size_t index, uintptr_t current, uintptr_t *low, uintptr_t *high) {
  *high = g_stack_regions[index].high;
  *low = index + 1 == g_stack_depth ? current : g_stack_regions[index].scan_low;
}
