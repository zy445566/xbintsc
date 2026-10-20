/* ------------------------------------------------------------------------- */
/* Strings                                                                   */
/* ------------------------------------------------------------------------- */

/* Concatenations up to this many bytes are copied eagerly. The copy is cheaper
 * than a cons node for the short strings that dominate everyday code, and a
 * loop that keeps appending crosses the threshold quickly, after which every
 * append is O(1). */
#define XT_STRING_CONS_THRESHOLD 32

xt_string *xt_as_string(xt_value v) {
  if (XT_IS_STRING(v)) return (xt_string *)XT_GET_PTR(v);
  return NULL;
}

int32_t xt_string_length(xt_string *s) { return (int32_t)s->length; }

const char *xt_string_bytes(xt_string *s) {
  if (!s) return NULL;
  if (s->flat) return s->flat;

  /* The tree is traversed depth-first without recursion: a loop that appends
   * 100k times produces a 100k-deep left spine, which would overflow the C
   * stack. Right children are pushed so the leftmost leaf is emitted first. */
  size_t total = (size_t)s->length;
  char *out = (char *)malloc(total + 1);
  if (!out) {
    fprintf(stderr, "xbintsc: out of memory flattening string\n");
    abort();
  }

  size_t capacity = 64;
  size_t count = 0;
  xt_string **stack = (xt_string **)malloc(sizeof(xt_string *) * capacity);
  if (!stack) {
    fprintf(stderr, "xbintsc: out of memory flattening string\n");
    abort();
  }
  stack[count++] = s;

  size_t offset = 0;
  while (count > 0) {
    xt_string *node = stack[--count];
    if (node->flat) {
      memcpy(out + offset, node->flat, node->length);
      offset += node->length;
      continue;
    }
    if (count + 2 > capacity) {
      while (count + 2 > capacity) capacity *= 2;
      xt_string **grown = (xt_string **)realloc(stack, sizeof(xt_string *) * capacity);
      if (!grown) {
        fprintf(stderr, "xbintsc: out of memory flattening string\n");
        abort();
      }
      stack = grown;
    }
    if (node->right) stack[count++] = node->right;
    if (node->left) stack[count++] = node->left;
  }
  free(stack);

  out[offset] = '\0';
  s->flat = out;
  s->capacity = (uint32_t)(total + 1);
  s->left = NULL;
  s->right = NULL;
  return s->flat;
}

static xt_value xt_string_concat(xt_string *a, xt_string *b) {
  if (a->length == 0) return XT_FROM_PTR(XT_TAG_STRING, b);
  if (b->length == 0) return XT_FROM_PTR(XT_TAG_STRING, a);
  size_t total = (size_t)a->length + (size_t)b->length;
  if (total <= XT_STRING_CONS_THRESHOLD) {
    xt_string *out = xt_string_alloc(total);
    memcpy(out->flat, xt_string_bytes(a), a->length);
    memcpy(out->flat + a->length, xt_string_bytes(b), b->length);
    return XT_FROM_PTR(XT_TAG_STRING, out);
  }
  xt_string *out = (xt_string *)xt_alloc(sizeof(xt_string), XT_OBJECT_KIND_STRING);
  out->length = (uint32_t)total;
  out->capacity = 0;
  out->flat = NULL;
  out->left = a;
  out->right = b;
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

int xt_string_equals(xt_string *a, xt_string *b) {
  /* Interned literals (property names above all) are shared, so identity is the
   * common case and skips the length, flattening and byte comparison entirely. */
  if (a == b) return 1;
  if (a->length != b->length) return 0;
  return memcmp(xt_string_bytes(a), xt_string_bytes(b), a->length) == 0;
}
