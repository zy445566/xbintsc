/*
 * xbintsc runtime — release the backing storage of one collected block.
 *
 * Split out of xt_alloc.c to keep the collector within the file-size budget;
 * called from the sweep with the block already unlinked from the heap list.
 */

#include "rt_internal.h"

#include <stdlib.h>

void xt_gc_release(xt_header *header) {
  const size_t block_size = (size_t)header->size;
  const int pooled = (header->flags & XT_GC_POOLED) != 0;
  switch (header->kind) {
    case XT_OBJECT_KIND_STRING:
      free(((xt_string *)header)->flat);
      break;
    case XT_OBJECT_KIND_BIGINT:
      free(((xt_bigint *)header)->limbs);
      break;
    case XT_OBJECT_KIND_OBJECT:
    case XT_OBJECT_KIND_ERROR:
      xt_raw_free(((xt_object *)header)->properties);
      break;
    case XT_OBJECT_KIND_ARRAY_BUFFER: {
      xt_object *object = (xt_object *)header;
      free(object->buffer_bytes);
      xt_raw_free(object->properties);
      break;
    }
    case XT_OBJECT_KIND_ARRAY:
      xt_raw_free(((xt_array *)header)->items);
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
    case XT_OBJECT_KIND_WEAK_MAP:
      free(((xt_weak_map *)header)->keys);
      free(((xt_weak_map *)header)->values);
      break;
    case XT_OBJECT_KIND_WEAK_SET:
      free(((xt_weak_set *)header)->values);
      break;
    case XT_OBJECT_KIND_PROMISE:
      xt_promise_gc_release(header);
      break;
    case XT_OBJECT_KIND_GENERATOR:
      xt_generator_gc_release(header);
      break;
    case XT_OBJECT_KIND_ASYNC:
      xt_async_gc_release(header);
      break;
    default:
      break;
  }
  if (pooled) {
    xt_pool_free_block(header, block_size);
  } else {
    free(header);
  }
}

