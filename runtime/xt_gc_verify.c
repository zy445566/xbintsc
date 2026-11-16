/*
 * `XT_GC_VERIFY=1`: check that the collector keeps every object a live object
 * points at.
 *
 * After the mark phase and *before* the sweep, walk every marked object's
 * payload conservatively (the same way the collector scans the stack) and
 * report each heap block it references that is not marked. A hit means either
 * the tracer for that object kind skips a field, or the referenced block was
 * never reachable from a root — both are the class of bug that ends in a
 * use-after-free far away from the collection.
 *
 * Debug-only: the walk is O(live objects × payload) and allocates nothing.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Lookup a raw address in the collector's table (xt_alloc.c). */
xt_header *xt_gc_lookup_raw(uintptr_t address);

static const char *xt_gc_kind_name(uint8_t kind) {
  switch (kind) {
    case XT_OBJECT_KIND_STRING: return "string";
    case XT_OBJECT_KIND_OBJECT: return "object";
    case XT_OBJECT_KIND_ARRAY: return "array";
    case XT_OBJECT_KIND_FUNCTION: return "function";
    case XT_OBJECT_KIND_PROMISE: return "promise";
    case XT_OBJECT_KIND_MAP: return "map";
    case XT_OBJECT_KIND_SET: return "set";
    case XT_OBJECT_KIND_DATE: return "date";
    case XT_OBJECT_KIND_REGEXP: return "regexp";
    case XT_OBJECT_KIND_SYMBOL: return "symbol";
    case XT_OBJECT_KIND_BIGINT: return "bigint";
    case XT_OBJECT_KIND_ERROR: return "error";
    case XT_OBJECT_KIND_GENERATOR: return "generator";
    case XT_OBJECT_KIND_ITERATOR: return "iterator";
    case XT_OBJECT_KIND_ASYNC: return "async";
    case XT_OBJECT_KIND_ASYNC_ITERATOR: return "async-iterator";
    case XT_OBJECT_KIND_WEAK_MAP: return "weakmap";
    case XT_OBJECT_KIND_WEAK_SET: return "weakset";
    case XT_OBJECT_KIND_WEAK_REF: return "weakref";
    default: return "?";
  }
}

/** Does `word` name a heap block (raw pointer or NaN-boxed value)? */
static xt_header *xt_gc_verify_word(uint64_t word) {
  if (XT_IS_STRING(word) || XT_IS_OBJECT(word) || XT_IS_ARRAY(word) || XT_IS_FUNCTION(word) ||
      XT_IS_BIGINT(word)) {
    return xt_gc_lookup_raw((uintptr_t)XT_GET_PTR(word));
  }
  return xt_gc_lookup_raw((uintptr_t)word);
}

void xt_gc_verify_payloads(void) {
  size_t misses = 0;
  for (xt_header *header = xt_gc_heap_head(); header != NULL; header = header->gc_next) {
    if ((header->flags & XT_GC_MARK) == 0) continue;
    if (header->size <= sizeof(xt_header)) continue;
    const uint64_t *words = (const uint64_t *)((const char *)header + sizeof(xt_header));
    size_t count = (header->size - sizeof(xt_header)) / sizeof(uint64_t);
    for (size_t i = 0; i < count; i++) {
      xt_header *target = xt_gc_verify_word(words[i]);
      if (target == NULL || (target->flags & XT_GC_MARK) != 0) continue;
      fprintf(stderr,
              "xbintsc: gc verify: %s(%u bytes) at word %zu references unmarked %s(%u bytes)\n",
              xt_gc_kind_name(header->kind), (unsigned)header->size, i,
              xt_gc_kind_name(target->kind), (unsigned)target->size);
      if (++misses >= 8) {
        fprintf(stderr, "xbintsc: gc verify: stopping after 8 reports\n");
        return;
      }
    }
  }
  if (misses == 0) fprintf(stderr, "xbintsc: gc verify: ok\n");
}

/* -- freed-block ring, for crash triage ----------------------------------- */

/* Every block the sweep releases is recorded here: a crash handler that knows
 * the faulting address can say whether it was inside a freed block. */

typedef struct {
  uintptr_t address;
  size_t size;
  uint8_t kind;
} xt_gc_freed_entry;

#define XT_GC_FREED_RING 512
static xt_gc_freed_entry g_freed[XT_GC_FREED_RING];
static size_t g_freed_count = 0;

void xt_gc_debug_record_free(uintptr_t address, size_t size, int kind) {
  xt_gc_freed_entry *entry = &g_freed[g_freed_count % XT_GC_FREED_RING];
  entry->address = address;
  entry->size = size;
  entry->kind = (uint8_t)kind;
  g_freed_count++;
}

/** Describe `address` if it falls inside a block the sweep released. */
int xt_gc_debug_describe(uintptr_t address, char *out, size_t capacity) {
  size_t live = g_freed_count < XT_GC_FREED_RING ? g_freed_count : XT_GC_FREED_RING;
  {
    xt_header *live_block = xt_gc_lookup_raw(address);
    snprintf(out, capacity,
             "    fault address 0x%llx (%zu frees recorded)%s\n",
             (unsigned long long)address, g_freed_count,
             live_block ? ", inside a LIVE block" : ", not in the live heap");
    if (live_block) return 1;
  }
  for (size_t i = 0; i < live; i++) {
    const xt_gc_freed_entry *entry = &g_freed[i];
    if (address < entry->address || address >= entry->address + entry->size) continue;
    snprintf(out, capacity,
             "    fault address 0x%llx is +0x%llx inside a %s block of %u bytes that the "
             "collector freed (%zu frees ago)\n",
             (unsigned long long)address, (unsigned long long)(address - entry->address),
             xt_gc_kind_name(entry->kind), (unsigned)entry->size, i);
    return 1;
  }
  return 0;
}
