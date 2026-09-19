/*
 * xbintsc runtime — heap allocation.
 *
 * A bump-style arena shared by every object kind. Allocations are never
 * reclaimed; the design keeps replacing this with a real collector isolated
 * behind `xt_alloc` (see rt_internal.h).
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */
/* Allocation                                                                */
/* ------------------------------------------------------------------------- */

static size_t g_heap_bytes = 0;
static size_t g_heap_allocations = 0;
static xt_header *g_heap_head = NULL;

void *xt_alloc(size_t size, int kind) {
  xt_header *header = (xt_header *)calloc(1, size);
  if (!header) {
    fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
    abort();
  }
  header->kind = (uint8_t)kind;
  header->size = (uint32_t)size;
  header->gc_next = g_heap_head;
  g_heap_head = header;
  g_heap_bytes += size;
  g_heap_allocations++;
  return header;
}

size_t xt_heap_bytes(void) { return g_heap_bytes; }
size_t xt_heap_allocations(void) { return g_heap_allocations; }
