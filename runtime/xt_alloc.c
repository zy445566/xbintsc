/*
 * xbintsc runtime — heap allocation and garbage collection.
 *
 * Non-moving mark-sweep over a `calloc`-ed block list; see `xt_gc_collect`.
 */

#include "rt_internal.h"

#include <setjmp.h>
#include <stdatomic.h>
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

/* `XT_GC_TRACE=1` prints one line per collection (live bytes, allocation count
 * and the next trigger) to stderr, for diagnosing heap growth. */
static int g_gc_trace = -1;
static size_t g_gc_collections = 0;

static int xt_gc_trace_enabled(void) {
  if (g_gc_trace < 0) {
    const char *env = getenv("XT_GC_TRACE");
    g_gc_trace = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
  }
  return g_gc_trace;
}

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
/*                                                                           */
/* The region table lives in `xt_gc_stack.c`; it grows on demand because an   */
/* async call chain can be deeper than any fixed limit.                      */
/* ------------------------------------------------------------------------- */

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

_Thread_local int xt_gc_worker_thread = 0;

/* Workers allocate result values, so linking takes a spinlock; a block born
 * while a collection runs is marked, or the sweep would free it. */
static atomic_flag g_heap_lock = ATOMIC_FLAG_INIT;

static void xt_heap_link(xt_header *header) {
  while (atomic_flag_test_and_set(&g_heap_lock)) {
  }
  header->gc_next = g_heap_head;
  g_heap_head = header;
  if (g_gc_running && header->kind < 32) header->flags |= XT_GC_MARK;
  g_heap_bytes += header->size;
  g_heap_allocations++;
  atomic_flag_clear(&g_heap_lock);
}

void *xt_alloc(size_t size, int kind) {
  XT_PROFILE_TICK(XT_PROFILE_ALLOC);
  if (xt_profile_on) xt_profile_note_alloc(kind);
  if (!g_gc_initialised) xt_gc_init();
  if (xt_gc_worker_thread) {
    void *block = calloc(1, size);
    if (!block) {
      fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
      abort();
    }
    xt_header *header = (xt_header *)block;
    header->kind = (uint8_t)kind;
    header->size = (uint32_t)size;
    xt_heap_link(header);
    return header;
  }
  /* Collect before handing out the new block: the block being allocated here
   * is guaranteed not to be swept, and references held by the caller survive
   * the conservative stack scan. */
  if (g_gc_armed && !g_gc_running && g_heap_bytes >= g_gc_threshold) xt_gc_collect();

  xt_header *header = (xt_header *)xt_pool_alloc(size);
  if (header != NULL) {
    header->kind = (uint8_t)kind;
    header->flags = XT_GC_POOLED;
    header->size = (uint32_t)size;
    xt_heap_link(header);
    return header;
  }
  header = (xt_header *)calloc(1, size);
  if (!header) {
    fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
    abort();
  }
  header->kind = (uint8_t)kind;
  header->flags = 0;
  header->size = (uint32_t)size;
  xt_heap_link(header);
  return header;
}

size_t xt_heap_bytes(void) { return g_heap_bytes; }
size_t xt_heap_allocations(void) { return g_heap_allocations; }

/* ------------------------------------------------------------------------- */
/* Collector configuration                                                   */
/* ------------------------------------------------------------------------- */

void xt_gc_set_stack_base(void *base) {
  uintptr_t high = (uintptr_t)base;
  if (high == 0) return;
  xt_gc_init_main_stack(high);
}

void xt_gc_init(void) {
  if (g_gc_initialised) return;
  g_gc_initialised = 1;
  /* The generated `main` calls this first, before any module code, so the
   * `performance.now()` origin is the start of the program. */
  xt_time_init();
  xt_profile_init();
  uintptr_t high = (uintptr_t)&high;
  /* `xt_gc_set_stack_base` normally installed a bound above the caller's frame
   * already; the helper keeps the higher of the two. */
  xt_gc_init_main_stack(high);
  const char *env = getenv("XT_GC_THRESHOLD");
  if (env && *env) {
    long long value = atoll(env);
    if (value > 0) {
      g_gc_threshold = (size_t)value;
      g_gc_threshold_pinned = 1;
    }
  }
  /* A crash is the one case where the JavaScript-level shadow stack is the
   * most useful thing to print; install the handler once per process. */
  xt_install_crash_handler();
}
void xt_gc_arm(void) { g_gc_armed = 1; }

/* Module initialisation runs with a much higher threshold: a bundle allocates
 * its module graph inside one generated function, and collecting in the middle
 * of it has produced access violations. Startup is bounded. */
#define XT_GC_STARTUP_THRESHOLD ((size_t)512 << 20) /* 512 MiB */

void xt_gc_begin_startup(void) {
  if (!g_gc_threshold_pinned) g_gc_threshold = XT_GC_STARTUP_THRESHOLD;
}
void xt_gc_end_startup(void) {
  if (!g_gc_threshold_pinned) g_gc_threshold = (size_t)16 << 20;
}
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
xt_header *xt_gc_lookup_raw(uintptr_t address);

/* Typed pointers (a promise's `reaction->promise`, a coroutine's active async
 * frame, ...) are only *probably* live: a stale one must be skipped, not
 * dereferenced. Dereferencing it here is how a crash turned into "read at
 * 0xffffffffffffffff" instead of a diagnosis. */
static void xt_gc_gray_push_checked(xt_header *header) {
  if (!header) return;
  if (xt_gc_lookup_raw((uintptr_t)header) == header) {
    xt_gc_gray_push(header);
    return;
  }
  xt_gc_verify_rejected((uintptr_t)header);
}
void xt_gc_mark_header(xt_header *header) { xt_gc_gray_push_checked(header); }

void xt_gc_mark_value(xt_value value) {
  /* One tag compare rejects every number (and undefined/null/booleans), which
   * is most of a scene's property slots; the five individual `XT_IS_*` tests
   * made marking a large live set spend most of its time in predictable
   * branches. */
  if ((value & XT_TAG_MASK) < XT_TAG_BIGINT) return;
  xt_gc_gray_push((xt_header *)XT_GET_PTR(value));
}
int xt_gc_value_is_marked(xt_value value) {
  if (!(XT_IS_STRING(value) || XT_IS_OBJECT(value) || XT_IS_ARRAY(value) ||
        XT_IS_FUNCTION(value) || XT_IS_BIGINT(value))) {
    return 0;
  }
  xt_header *header = xt_gc_table_lookup((uintptr_t)XT_GET_PTR(value));
  return header != NULL && (header->flags & XT_GC_MARK) != 0;
}
xt_header *xt_gc_heap_head(void) { return g_heap_head; }

xt_header *xt_gc_lookup_raw(uintptr_t address) { return xt_gc_table_lookup(address); }

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
    case XT_OBJECT_KIND_STRING: {
      /* A flat string holds bytes only. A cons node is an immutable rope whose
       * children must stay alive until `xt_string_bytes` materialises it. */
      xt_string *string = (xt_string *)header;
      if (string->flat == NULL) {
        if (string->left) xt_gc_gray_push_checked(&string->left->header);
        if (string->right) xt_gc_gray_push_checked(&string->right->header);
      }
      return;
    }
    case XT_OBJECT_KIND_BIGINT:
    case XT_OBJECT_KIND_DATE:
      /* Numbers/bytes only: no references to follow. */
      return;
    case XT_OBJECT_KIND_OBJECT:
    case XT_OBJECT_KIND_ERROR:
    case XT_OBJECT_KIND_ARRAY_BUFFER: {
      /* Precise trace: every reference in an ordinary object is an `xt_value`
       * (a property key/value/accessor, the prototype and the captured error
       * stack). The old conservative payload scan also looked up the raw
       * `properties`/`shape`/`hooks` pointers and every number in the property
       * table, which made marking a large scene spend most of its time in the
       * heap address table. */
      xt_object *object = (xt_object *)header;
      xt_gc_mark_value(object->prototype);
      xt_gc_mark_value(object->error_stack);
      for (uint32_t i = 0; i < object->count; i++) {
        xt_gc_mark_value(object->properties[i].key);
        xt_gc_mark_value(object->properties[i].value);
        xt_gc_mark_value(object->properties[i].getter);
        xt_gc_mark_value(object->properties[i].setter);
      }
      return;
    }
    case XT_OBJECT_KIND_ARRAY: {
      xt_array *array = (xt_array *)header;
      xt_gc_mark_value(array->prototype);
      xt_gc_mark_value(array->extra);
      for (uint32_t i = 0; i < array->length; i++) xt_gc_mark_value(array->items[i]);
      return;
    }
    case XT_OBJECT_KIND_FUNCTION: {
      xt_function *function = (xt_function *)header;
      if (function->name != NULL) xt_gc_gray_push_checked(&function->name->header);
      if (function->properties != NULL) xt_gc_gray_push_checked(&function->properties->header);
      xt_gc_mark_value(function->prototype);
      for (int32_t i = 0; i < function->environment_count; i++) {
        xt_gc_mark_value(function->environment[i]);
      }
      return;
    }
    case XT_OBJECT_KIND_MAP: {
      xt_map *map = (xt_map *)header;
      for (uint32_t i = 0; i < map->count; i++) {
        xt_gc_mark_value(map->keys[i]);
        xt_gc_mark_value(map->values[i]);
      }
      return;
    }
    case XT_OBJECT_KIND_SET: {
      xt_set_object *set = (xt_set_object *)header;
      for (uint32_t i = 0; i < set->count; i++) xt_gc_mark_value(set->values[i]);
      return;
    }
    case XT_OBJECT_KIND_REGEXP: {
      xt_regexp *regexp = (xt_regexp *)header;
      xt_gc_mark_value(regexp->source);
      xt_gc_mark_value(regexp->flags);
      return;
    }
    case XT_OBJECT_KIND_PROMISE:
      xt_promise_gc_trace(header);
      return;
    case XT_OBJECT_KIND_GENERATOR:
      xt_generator_gc_trace(header);
      return;
    case XT_OBJECT_KIND_ASYNC:
      xt_async_gc_trace(header);
      return;
    case XT_OBJECT_KIND_ASYNC_ITERATOR:
      xt_async_iterator_gc_trace(header);
      return;
    case XT_OBJECT_KIND_WEAK_MAP:
    case XT_OBJECT_KIND_WEAK_SET:
    case XT_OBJECT_KIND_WEAK_REF:
      /* Weak references are not traced; `xt_weak_gc_*` owns the ephemeron
       * fixed point after the ordinary mark phase. */
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
  size_t regionCount = xt_gc_stack_region_count();
  for (size_t i = 0; i < regionCount; i++) {
    uintptr_t high = 0;
    uintptr_t low = 0;
    xt_gc_stack_region_bounds(i, stack_pointer, &low, &high);
    if (high > low) xt_gc_scan_region((const void *)low, high - low);
  }
}
void xt_gc_collect(void) {
  XT_PROFILE_TICK(XT_PROFILE_GC);
  if (g_gc_running) return;
  if (!g_gc_initialised) xt_gc_init();
  g_gc_running = 1;
  const double gc_started_ms = xt_monotonic_ms();
  const int trace = xt_gc_trace_enabled();
  const int leak = getenv("XT_GC_LEAK") != NULL;
  const size_t heap_before = g_heap_bytes;
  const size_t allocations_before = g_heap_allocations;

  const double phase_started = xt_monotonic_ms();
  xt_gc_table_build();
  const double phase_table = xt_monotonic_ms() - phase_started;
  const double mark_started = xt_monotonic_ms();
  xt_gc_mark_roots();
  /* Ephemeron fixed point: a WeakMap value is reachable only when its key is
   * reachable. Marking a value can reach further keys, so repeat until a pass
   * marks nothing new. WeakRefs are cleared only after this settles, because
   * an ephemeron may still keep their target alive. */
  for (;;) {
    while (g_gray_count > 0) {
      xt_header *header = g_gray[--g_gray_count];
      xt_gc_process(header);
    }
    if (!xt_weak_gc_process_once()) break;
  }
  xt_weak_gc_clear();
  xt_weak_gc_compact();
  if (getenv("XT_GC_VERIFY") != NULL) xt_gc_verify_payloads();
  const double phase_mark = xt_monotonic_ms() - mark_started;
  const double sweep_started = xt_monotonic_ms();

  xt_header **link = &g_heap_head;
  xt_header *header = g_heap_head;
  size_t live_by_kind[32];
  size_t bytes_by_kind[32];
  for (int i = 0; i < 32; i++) {
    live_by_kind[i] = 0;
    bytes_by_kind[i] = 0;
  }
  while (header) {
    xt_header *next = header->gc_next;
    if (header->flags & XT_GC_MARK) {
      header->flags &= (uint8_t)~XT_GC_MARK;
      link = &header->gc_next;
      if (trace) {
        size_t kind = header->kind < 32 ? header->kind : 0;
        live_by_kind[kind]++;
        bytes_by_kind[kind] += header->size;
      }
    } else {
      *link = next;
      g_heap_bytes -= header->size;
      g_heap_allocations--;
      xt_gc_debug_record_free((uintptr_t)header, header->size, header->kind);
      if (!leak) xt_gc_release(header);
    }
    header = next;
  }

  xt_gc_table_free();
  const double phase_sweep = xt_monotonic_ms() - sweep_started;
  g_gc_running = 0;

  if (!g_gc_threshold_pinned) {
    /* Follow the live set: four times the surviving heap is the headroom a
     * mark-sweep wants between collections for an interactive program. Two
     * times collected too often: a three.js scene that keeps ~380k objects live
     * spent a ~300 ms pause every ~1.2 s, and half of the frame budget went to
     * collections. Four times halves that overhead at a bounded memory cost
     * (the threshold still shrinks with the live set, so it cannot ratchet
     * upwards). The floor keeps a tiny heap from collecting on nearly every
     * allocation. */
    size_t next = g_heap_bytes * 4;
    if (next < XT_GC_MIN_THRESHOLD) next = XT_GC_MIN_THRESHOLD;
    g_gc_threshold = next;
  }

  g_gc_collections++;
  if (trace) {
    fprintf(stderr,
            "xt_gc: #%zu heap %zu -> %zu bytes, allocations %zu -> %zu, next trigger %zu, took %.1fms"
            " (table %.1f, mark %.1f, sweep %.1f)\n",
            g_gc_collections, heap_before, g_heap_bytes, allocations_before, g_heap_allocations,
            g_gc_threshold, xt_monotonic_ms() - gc_started_ms, phase_table, phase_mark,
            phase_sweep);
    fprintf(stderr, "xt_gc:   live kinds");
    for (int i = 1; i < 16; i++) {
      if (live_by_kind[i] != 0) {
        fprintf(stderr, " %d:%zu/%zuB", i, live_by_kind[i], bytes_by_kind[i]);
      }
    }
    fprintf(stderr, "\n");
  }
}
