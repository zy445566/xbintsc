/* ------------------------------------------------------------------------- */
/* Allocation and basic helpers                                              */
/* ------------------------------------------------------------------------- */

static xt_bigint *bi_alloc(uint32_t capacity) {
  if (capacity < 1) capacity = 1;
  xt_bigint *b = (xt_bigint *)xt_alloc(sizeof(xt_bigint), XT_OBJECT_KIND_BIGINT);
  b->sign = 0;
  b->length = 0;
  b->capacity = capacity;
  b->limbs = (uint32_t *)calloc(capacity, sizeof(uint32_t));
  if (!b->limbs) {
    fprintf(stderr, "xbintsc: out of memory allocating bigint\n");
    abort();
  }
  return b;
}

static void bi_reserve(xt_bigint *b, uint32_t capacity) {
  if (capacity <= b->capacity) return;
  uint32_t grown = b->capacity ? b->capacity : 1;
  while (grown < capacity) grown *= 2;
  uint32_t *limbs = (uint32_t *)realloc(b->limbs, (size_t)grown * sizeof(uint32_t));
  if (!limbs) {
    fprintf(stderr, "xbintsc: out of memory growing bigint\n");
    abort();
  }
  memset(limbs + b->capacity, 0, (size_t)(grown - b->capacity) * sizeof(uint32_t));
  b->limbs = limbs;
  b->capacity = grown;
}

/** Strip leading zero limbs and canonicalise the sign of zero. */
static void bi_normalize(xt_bigint *b) {
  while (b->length > 0 && b->limbs[b->length - 1] == 0) b->length--;
  if (b->length == 0) b->sign = 0;
}

static xt_bigint *bi_zero(void) { return bi_alloc(1); }

static xt_bigint *bi_copy(const xt_bigint *src) {
  xt_bigint *r = bi_alloc(src->length ? src->length : 1);
  if (src->length) memcpy(r->limbs, src->limbs, (size_t)src->length * sizeof(uint32_t));
  r->length = src->length;
  r->sign = src->sign;
  return r;
}

static xt_value bi_wrap(xt_bigint *b) { return XT_FROM_PTR(XT_TAG_BIGINT, b); }

#define BI(v) ((xt_bigint *)XT_GET_PTR(v))

static void bi_throw_type(const char *message) {
  size_t len = strlen(message);
  char *buffer = (char *)malloc(len + 16);
  snprintf(buffer, len + 16, "TypeError: %s", message);
  xt_throw(xt_string_from_cstr(buffer));
  free(buffer);
}

static void bi_throw_range(const char *message) {
  size_t len = strlen(message);
  char *buffer = (char *)malloc(len + 16);
  snprintf(buffer, len + 16, "RangeError: %s", message);
  xt_throw(xt_string_from_cstr(buffer));
  free(buffer);
}

static void bi_throw_syntax(const char *message) {
  size_t len = strlen(message);
  char *buffer = (char *)malloc(len + 16);
  snprintf(buffer, len + 16, "SyntaxError: %s", message);
  xt_throw(xt_string_from_cstr(buffer));
  free(buffer);
}

static xt_bigint *bi_from_u64(int sign, uint64_t magnitude) {
  xt_bigint *r = bi_alloc(2);
  if (magnitude == 0) return r;
  r->limbs[0] = (uint32_t)magnitude;
  r->limbs[1] = (uint32_t)(magnitude >> 32);
  r->length = r->limbs[1] ? 2 : 1;
  r->sign = sign;
  return r;
}

xt_value xt_bigint_from_i64(int64_t value) {
  if (value == 0) return bi_wrap(bi_zero());
  uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
  return bi_wrap(bi_from_u64(value < 0 ? -1 : 1, magnitude));
}

