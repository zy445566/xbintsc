/*
 * xbintsc runtime — heap allocation and garbage collection.
 *
 * Every heap object is a `calloc`-backed block whose first bytes are an
 * `xt_header`. Live blocks are threaded through `g_heap_head` (`gc_next`), and
 * the collector is a non-moving mark-sweep over that list:
 *
 *   1. build an address -> header hash set of every live allocation;
 *   2. mark from the roots — registered slots, root providers and a
 *      conservative scan of the active C stack (the whole `jmp_buf` plus the
 *      stack words up to the region's high bound);
 *   3. trace each marked object. Value fields are visited precisely where the
 *      layout is known, but the object payload and its separately allocated
 *      backing buffers are also scanned conservatively, so a raw pointer held
 *      by older code is still followed;
 *   4. sweep: unmarked blocks are released (including their backing buffers)
 *      and unlinked, survivors keep their block and have the mark bit cleared.
 *
 * A non-moving collector keeps the NaN-boxed `xt_value` representation
 * untouched: a pointer never changes, so no write barriers or handle
 * indirection are needed. `gc_next` doubles as the live list and, once a block
 * is released, the memory simply returns to `malloc`.
 *
 * The collector is single-threaded, like the rest of the runtime (workers are
 * child processes, not threads).
 */

#include "rt_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Heap state                                                                */
/* ------------------------------------------------------------------------- */

static size_t g_heap_bytes = 0;
static size_t g_heap_allocations = 0;
static xt_header *g_heap_head = NULL;

static int g_gc_initialised = 0;
static int g_gc_armed = 0;
static int g_gc_running = 0;
static int g_gc_threshold_pinned = 0;
static size_t g_gc_threshold = (size_t)16 << 20; /* 16 MiB */
#define XT_GC_MIN_THRESHOLD ((size_t)1 << 20)

/* ------------------------------------------------------------------------- */
/* Roots                                                                     */
/* ------------------------------------------------------------------------- */

static xt_value **g_root_slots = NULL;
static size_t g_root_count = 0;
static size_t g_root_capacity = 0;

static void (**g_root_providers)(void) = NULL;
static size_t g_root_provider_count = 0;
static size_t g_root_provider_capacity = 0;

/* ------------------------------------------------------------------------- */
/* Active stack regions                                                      */
/* ------------------------------------------------------------------------- */

#define XT_GC_MAX_STACKS 64
typedef struct {
  uintptr_t low;
  uintptr_t high;
  /* Lower bound captured from the stack below this region when a deeper region
   * was entered on top of it. `UINTPTR_MAX` means "this is the top region"; the
   * collector then scans from the current stack pointer. */
  uintptr_t scan_low;
} xt_stack_region;

static xt_stack_region g_stack_regions[XT_GC_MAX_STACKS];
static int g_stack_depth = 0;

/* ------------------------------------------------------------------------- */
/* Mark stack and address set                                                */
/* ------------------------------------------------------------------------- */

static xt_header **g_gray = NULL;
static size_t g_gray_count = 0;
static size_t g_gray_capacity = 0;

static xt_header **g_table = NULL;
static size_t g_table_capacity = 0;
static size_t g_table_mask = 0;

static size_t xt_gc_hash(uintptr_t pointer) {
  uint64_t mixed = (uint64_t)(pointer >> 4) * 0x9E3779B97F4A7C15ULL;
  return (size_t)(mixed >> 17);
}

static xt_header *xt_gc_table_lookup(uintptr_t pointer) {
  if (g_table_capacity == 0) return NULL;
  size_t slot = xt_gc_hash(pointer) & g_table_mask;
  for (;;) {
    xt_header *header = g_table[slot];
    if (!header) return NULL;
    if ((uintptr_t)header == pointer) return header;
    slot = (slot + 1) & g_table_mask;
  }
}

static void xt_gc_table_build(void) {
  size_t capacity = 16;
  while (capacity < (g_heap_allocations + 1) * 2) capacity <<= 1;
  free(g_table);
  g_table = (xt_header **)calloc(capacity, sizeof(xt_header *));
  if (!g_table) {
    fprintf(stderr, "xbintsc: out of memory building GC table\n");
    abort();
  }
  g_table_capacity = capacity;
  g_table_mask = capacity - 1;
  for (xt_header *header = g_heap_head; header; header = header->gc_next) {
    size_t slot = xt_gc_hash((uintptr_t)header) & g_table_mask;
    while (g_table[slot]) slot = (slot + 1) & g_table_mask;
    g_table[slot] = header;
  }
}

static void xt_gc_table_free(void) {
  free(g_table);
  g_table = NULL;
  g_table_capacity = 0;
  g_table_mask = 0;
}

/* ------------------------------------------------------------------------- */
/* Allocation                                                                */
/* ------------------------------------------------------------------------- */

void *xt_alloc(size_t size, int kind) {
  if (!g_gc_initialised) xt_gc_init();
  /* Collect before handing out the new block: the block being allocated here
   * is guaranteed not to be swept, and references held by the caller survive
   * the conservative stack scan. */
  if (g_gc_armed && !g_gc_running && g_heap_bytes >= g_gc_threshold) xt_gc_collect();

  xt_header *header = (xt_header *)calloc(1, size);
  if (!header) {
    fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
    abort();
  }
  header->kind = (uint8_t)kind;
  header->flags = 0;
  header->size = (uint32_t)size;
  header->gc_next = g_heap_head;
  g_heap_head = header;
  g_heap_bytes += size;
  g_heap_allocations++;
  return header;
}

size_t xt_heap_bytes(void) { return g_heap_bytes; }
size_t xt_heap_allocations(void) { return g_heap_allocations; }

/* ------------------------------------------------------------------------- */
/* Collector configuration                                                   */
/* ------------------------------------------------------------------------- */

void xt_gc_init(void) {
  if (g_gc_initialised) return;
  g_gc_initialised = 1;
  uintptr_t high = (uintptr_t)&high;
  if (g_stack_depth == 0) {
    g_stack_regions[0].low = 0;
    g_stack_regions[0].high = high;
    g_stack_regions[0].scan_low = UINTPTR_MAX;
    g_stack_depth = 1;
  }
  const char *env = getenv("XT_GC_THRESHOLD");
  if (env && *env) {
    long long value = atoll(env);
    if (value > 0) {
      g_gc_threshold = (size_t)value;
      g_gc_threshold_pinned = 1;
    }
  }
}

void xt_gc_arm(void) { g_gc_armed = 1; }

void xt_gc_set_threshold(size_t bytes) {
  g_gc_threshold = bytes > XT_GC_MIN_THRESHOLD ? bytes : XT_GC_MIN_THRESHOLD;
  g_gc_threshold_pinned = 1;
}

void xt_gc_add_root(xt_value *slot) {
  if (!slot) return;
  for (size_t i = 0; i < g_root_count; i++) {
    if (g_root_slots[i] == slot) return;
  }
  if (g_root_count == g_root_capacity) {
    size_t capacity = g_root_capacity ? g_root_capacity * 2 : 32;
    xt_value **grown = (xt_value **)realloc(g_root_slots, sizeof(xt_value *) * capacity);
    if (!grown) abort();
    g_root_slots = grown;
    g_root_capacity = capacity;
  }
  g_root_slots[g_root_count++] = slot;
}

void xt_gc_add_root_range(xt_value *base, size_t count) {
  for (size_t i = 0; i < count; i++) xt_gc_add_root(base + i);
}

void xt_gc_register_root_provider(void (*provider)(void)) {
  if (!provider) return;
  for (size_t i = 0; i < g_root_provider_count; i++) {
    if (g_root_providers[i] == provider) return;
  }
  if (g_root_provider_count == g_root_provider_capacity) {
    size_t capacity = g_root_provider_capacity ? g_root_provider_capacity * 2 : 8;
    void (**grown)(void) = (void (**)(void))realloc(g_root_providers, sizeof(void (*)(void)) * capacity);
    if (!grown) abort();
    g_root_providers = grown;
    g_root_provider_capacity = capacity;
  }
  g_root_providers[g_root_provider_count++] = provider;
}

void xt_gc_push_stack(uintptr_t low, uintptr_t high) {
  if (g_stack_depth >= XT_GC_MAX_STACKS) return;
  /* The region below this one is now suspended: remember how far up its stack
   * was live so a collection taken while the new region runs still scans it. */
  uintptr_t here = (uintptr_t)&high;
  if (g_stack_depth > 0) g_stack_regions[g_stack_depth - 1].scan_low = here;
  g_stack_regions[g_stack_depth].low = low;
  g_stack_regions[g_stack_depth].high = high;
  g_stack_regions[g_stack_depth].scan_low = low;
  g_stack_depth++;
}

void xt_gc_pop_stack(void) {
  if (g_stack_depth <= 1) return;
  g_stack_depth--;
  /* Back on the underlying stack: resume scanning it from the current frame. */
  if (g_stack_depth > 0) g_stack_regions[g_stack_depth - 1].scan_low = UINTPTR_MAX;
}

/* ------------------------------------------------------------------------- */
/* Marking                                                                   */
/* ------------------------------------------------------------------------- */

static void xt_gc_gray_push(xt_header *header) {
  if (!header || (header->flags & XT_GC_MARK)) return;
  header->flags |= XT_GC_MARK;
  if (g_gray_count == g_gray_capacity) {
    size_t capacity = g_gray_capacity ? g_gray_capacity * 2 : 256;
    xt_header **grown = (xt_header **)realloc(g_gray, sizeof(xt_header *) * capacity);
    if (!grown) abort();
    g_gray = grown;
    g_gray_capacity = capacity;
  }
  g_gray[g_gray_count++] = header;
}

void xt_gc_mark_header(xt_header *header) { xt_gc_gray_push(header); }

void xt_gc_mark_value(xt_value value) {
  if (XT_IS_STRING(value) || XT_IS_OBJECT(value) || XT_IS_ARRAY(value) || XT_IS_FUNCTION(value) ||
      XT_IS_BIGINT(value)) {
    xt_gc_gray_push((xt_header *)XT_GET_PTR(value));
  }
}

void xt_gc_scan_region(const void *base, size_t bytes) {
  if (!base) return;
  const uint64_t *words = (const uint64_t *)base;
  size_t count = bytes / sizeof(uint64_t);
  for (size_t i = 0; i < count; i++) {
    uint64_t word = words[i];
    xt_header *header = NULL;
    if (XT_IS_STRING(word) || XT_IS_OBJECT(word) || XT_IS_ARRAY(word) || XT_IS_FUNCTION(word) ||
        XT_IS_BIGINT(word)) {
      header = xt_gc_table_lookup((uintptr_t)XT_GET_PTR(word));
    } else {
      header = xt_gc_table_lookup((uintptr_t)word);
    }
    if (header) xt_gc_gray_push(header);
  }
}

/* ------------------------------------------------------------------------- */
/* Tracing                                                                   */
/* ------------------------------------------------------------------------- */

/* Visit the payload (everything after the header) plus any separately
 * allocated backing buffer owned by the object. */
static void xt_gc_process(xt_header *header) {
  size_t payload = header->size > sizeof(xt_header) ? header->size - sizeof(xt_header) : 0;
  const void *base = (const char *)header + sizeof(xt_header);

  switch (header->kind) {
    case XT_OBJECT_KIND_STRING:
    case XT_OBJECT_KIND_BIGINT:
    case XT_OBJECT_KIND_DATE:
      /* Numbers/bytes only: no references to follow. */
      return;
    case XT_OBJECT_KIND_OBJECT:
    case XT_OBJECT_KIND_ERROR: {
      xt_object *object = (xt_object *)header;
      xt_gc_scan_region(base, payload);
      if (object->properties && object->count) {
        xt_gc_scan_region(object->properties, sizeof(xt_property) * (size_t)object->count);
      }
      return;
    }
    case XT_OBJECT_KIND_ARRAY: {
      xt_array *array = (xt_array *)header;
      xt_gc_scan_region(base, payload);
      if (array->items && array->length) {
        xt_gc_scan_region(array->items, sizeof(xt_value) * (size_t)array->length);
      }
      return;
    }
    case XT_OBJECT_KIND_FUNCTION: {
      xt_function *function = (xt_function *)header;
      xt_gc_scan_region(base, payload);
      if (function->environment && function->environment_count > 0) {
        xt_gc_scan_region(function->environment, sizeof(xt_value) * (size_t)function->environment_count);
      }
      return;
    }
    case XT_OBJECT_KIND_MAP: {
      xt_map *map = (xt_map *)header;
      xt_gc_scan_region(base, payload);
      if (map->keys && map->count) xt_gc_scan_region(map->keys, sizeof(xt_value) * (size_t)map->count);
      if (map->values && map->count) xt_gc_scan_region(map->values, sizeof(xt_value) * (size_t)map->count);
      return;
    }
    case XT_OBJECT_KIND_SET: {
      xt_set_object *set = (xt_set_object *)header;
      xt_gc_scan_region(base, payload);
      if (set->values && set->count) xt_gc_scan_region(set->values, sizeof(xt_value) * (size_t)set->count);
      return;
    }
    case XT_OBJECT_KIND_PROMISE:
      xt_promise_gc_trace(header);
      return;
    case XT_OBJECT_KIND_GENERATOR:
      xt_generator_gc_trace(header);
      return;
    default:
      /* Symbols, regexps, iterators and any future kind whose references live
       * inline: the payload scan covers them. */
      xt_gc_scan_region(base, payload);
      return;
  }
}

/* ------------------------------------------------------------------------- */
/* Release                                                                   */
/* ------------------------------------------------------------------------- */

static void xt_gc_release(xt_header *header) {
  switch (header->kind) {
    case XT_OBJECT_KIND_STRING:
      free(((xt_string *)header)->data);
      break;
    case XT_OBJECT_KIND_BIGINT:
      free(((xt_bigint *)header)->limbs);
      break;
    case XT_OBJECT_KIND_OBJECT:
    case XT_OBJECT_KIND_ERROR:
      free(((xt_object *)header)->properties);
      break;
    case XT_OBJECT_KIND_ARRAY:
      free(((xt_array *)header)->items);
      break;
    case XT_OBJECT_KIND_FUNCTION:
      free(((xt_function *)header)->environment);
      break;
    case XT_OBJECT_KIND_MAP:
      free(((xt_map *)header)->keys);
      free(((xt_map *)header)->values);
      break;
    case XT_OBJECT_KIND_SET:
      free(((xt_set_object *)header)->values);
      break;
    case XT_OBJECT_KIND_PROMISE:
      xt_promise_gc_release(header);
      break;
    case XT_OBJECT_KIND_GENERATOR:
      xt_generator_gc_release(header);
      break;
    default:
      break;
  }
  free(header);
}

/* ------------------------------------------------------------------------- */
/* Collection                                                                */
/* ------------------------------------------------------------------------- */

static void xt_gc_mark_roots(void) {
  for (size_t i = 0; i < g_root_count; i++) {
    if (g_root_slots[i]) xt_gc_mark_value(*g_root_slots[i]);
  }
  for (size_t i = 0; i < g_root_provider_count; i++) g_root_providers[i]();

  /* Callee-saved registers captured by setjmp, plus every active stack region.
   * Scanning more than the top region matters while a generator runs: the
   * frames that resumed it are still live on the main stack. */
  jmp_buf registers;
  _setjmp(registers);
  xt_gc_scan_region(&registers, sizeof(registers));
  uintptr_t stack_pointer = (uintptr_t)&registers;
  for (int i = 0; i < g_stack_depth; i++) {
    uintptr_t high = g_stack_regions[i].high;
    uintptr_t low = (i == g_stack_depth - 1) ? stack_pointer : g_stack_regions[i].scan_low;
    if (high > low) xt_gc_scan_region((const void *)low, high - low);
  }
}

void xt_gc_collect(void) {
  if (g_gc_running) return;
  if (!g_gc_initialised) xt_gc_init();
  g_gc_running = 1;

  xt_gc_table_build();
  xt_gc_mark_roots();
  while (g_gray_count > 0) {
    xt_header *header = g_gray[--g_gray_count];
    xt_gc_process(header);
  }

  xt_header **link = &g_heap_head;
  xt_header *header = g_heap_head;
  while (header) {
    xt_header *next = header->gc_next;
    if (header->flags & XT_GC_MARK) {
      header->flags &= (uint8_t)~XT_GC_MARK;
      link = &header->gc_next;
    } else {
      *link = next;
      g_heap_bytes -= header->size;
      g_heap_allocations--;
      xt_gc_release(header);
    }
    header = next;
  }

  xt_gc_table_free();
  g_gc_running = 0;

  if (!g_gc_threshold_pinned) {
    if (g_gc_threshold < g_heap_bytes * 2) g_gc_threshold = g_heap_bytes * 2;
    if (g_gc_threshold < XT_GC_MIN_THRESHOLD) g_gc_threshold = XT_GC_MIN_THRESHOLD;
  }
}
