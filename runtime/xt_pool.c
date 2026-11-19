/*
 * xbintsc runtime — the small-block allocator.
 *
 * The collector in `xt_alloc.c` owns reachability; this unit owns the bytes.
 * Keeping the two apart keeps each file within the repository's size budget and
 * makes the ownership rule explicit: every block either comes from a size-class
 * free list or from `malloc`, and the collector asks this unit to release it.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* ------------------------------------------------------------------------- */
/* Small-block pool                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Most runtime objects are small — an `xt_object` is ~120 bytes and a closure
 * ~100 — and a scene build creates millions of them. The C allocator charges a
 * lock, a free-list search and zeroing per block, which made object creation
 * the dominant cost of three.js's geometry and level construction. Blocks up
 * to `XT_POOL_SMALL_MAX` are carved out of 1 MiB slabs and recycled through
 * per-class free lists instead; the collector returns a block to its class, so
 * a steady state reuses the same memory.
 *
 * The pool is main-thread only: an I/O worker allocates result values with
 * `calloc` (see `xt_alloc`), and the collector — the only thing that releases
 * blocks — runs on the main thread. Slabs are never returned to the OS; the
 * pool is bounded by the process's peak live set, the same bound the
 * collector's threshold follows.
 */
#define XT_POOL_SMALL_MAX 2048
static const uint16_t xt_pool_sizes[] = {
    16,   32,   48,   64,   80,   96,   112,  128,  160,  192,  224,  256,
    320,  384,  448,  512,  640,  768,  896,  1024, 1280, 1536, 1792, 2048,
};
#define XT_POOL_CLASS_COUNT ((int)(sizeof(xt_pool_sizes) / sizeof(xt_pool_sizes[0])))
static void *g_pool_free[XT_POOL_CLASS_COUNT];
static unsigned char *g_pool_cursor = NULL;
static size_t g_pool_left = 0;

static int xt_pool_class_of(size_t size) {
  for (int index = 0; index < XT_POOL_CLASS_COUNT; index++) {
    if (size <= (size_t)xt_pool_sizes[index]) return index;
  }
  return -1;
}

/** A zeroed block of at least `size` bytes, or NULL when the size is large. */
void *xt_pool_alloc(size_t size) {
  int cls = xt_pool_class_of(size);
  if (cls < 0) return NULL;
  void *block = g_pool_free[cls];
  if (block != NULL) {
    g_pool_free[cls] = *(void **)block;
  } else {
    size_t need = (size_t)xt_pool_sizes[cls];
    if (g_pool_left < need) {
      size_t chunk = (size_t)1 << 20;
      if (chunk < need) chunk = need;
      g_pool_cursor = (unsigned char *)malloc(chunk);
      if (g_pool_cursor == NULL) return NULL;
      g_pool_left = chunk;
    }
    block = g_pool_cursor;
    g_pool_cursor += need;
    g_pool_left -= need;
  }
  memset(block, 0, size);
  return block;
}

/** Return a block from `xt_pool_alloc` to its class. */
void xt_pool_free_block(void *block, size_t size) {
  /* Only the main thread owns the pool. A worker never allocates from it, so
   * reaching here on a worker would mean a main-thread block was re-grown
   * there; leaking it is safer than corrupting the free list. */
  if (xt_gc_worker_thread) return;
  int cls = xt_pool_class_of(size);
  if (cls < 0 || block == NULL) return;
  *(void **)block = g_pool_free[cls];
  g_pool_free[cls] = block;
}

/* ------------------------------------------------------------------------- */
/* Small-block buffers (property tables, array items)                        */
/* ------------------------------------------------------------------------- */

/*
 * The buffers behind an object's property table and an array's items grow by
 * doubling, so they are allocated outside the collector. They get the same
 * pool treatment through a small private header, which also records where the
 * block came from: a buffer allocated on an I/O worker comes from `malloc` and
 * is released with `free`, while a main-thread buffer is pooled. The 16-byte
 * header keeps the payload 16-byte aligned.
 */
typedef struct {
  uint32_t cls;
  uint32_t size;
  uint64_t padding;
} xt_raw_header;

#define XT_RAW_HEADER_SIZE ((size_t)16)

void *xt_raw_alloc(size_t size) {
  if (size == 0) size = 1;
  unsigned char *block = NULL;
  if (!xt_gc_worker_thread && size <= XT_POOL_SMALL_MAX) block = (unsigned char *)xt_pool_alloc(size + XT_RAW_HEADER_SIZE);
  if (block == NULL) {
    block = (unsigned char *)malloc(size + XT_RAW_HEADER_SIZE);
    if (block == NULL) {
      fprintf(stderr, "xbintsc: out of memory allocating %zu bytes\n", size);
      abort();
    }
    xt_raw_header *header = (xt_raw_header *)block;
    header->cls = 0xffffffffu;
    header->size = (uint32_t)size;
    header->padding = 0;
  } else {
    xt_raw_header *header = (xt_raw_header *)block;
    header->cls = (uint32_t)xt_pool_class_of(size + XT_RAW_HEADER_SIZE);
    header->size = (uint32_t)size;
    header->padding = 0;
  }
  return block + XT_RAW_HEADER_SIZE;
}

void xt_raw_free(void *pointer) {
  if (pointer == NULL) return;
  unsigned char *block = (unsigned char *)pointer - XT_RAW_HEADER_SIZE;
  xt_raw_header *header = (xt_raw_header *)block;
  if (header->cls == 0xffffffffu) {
    free(block);
    return;
  }
  xt_pool_free_block(block, (size_t)header->size + XT_RAW_HEADER_SIZE);
}

void *xt_raw_realloc(void *pointer, size_t new_size) {
  if (pointer == NULL) return xt_raw_alloc(new_size);
  unsigned char *block = (unsigned char *)pointer - XT_RAW_HEADER_SIZE;
  xt_raw_header *header = (xt_raw_header *)block;
  size_t old_size = (size_t)header->size;
  void *grown = xt_raw_alloc(new_size);
  memcpy(grown, pointer, old_size < new_size ? old_size : new_size);
  xt_raw_free(pointer);
  return grown;
}

