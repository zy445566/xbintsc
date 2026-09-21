/* -- magnitude helpers ----------------------------------------------------- */

static int bi_cmp_mag(const xt_bigint *a, const xt_bigint *b) {
  if (a->length != b->length) return a->length < b->length ? -1 : 1;
  for (uint32_t i = a->length; i-- > 0;) {
    if (a->limbs[i] != b->limbs[i]) return a->limbs[i] < b->limbs[i] ? -1 : 1;
  }
  return 0;
}

static xt_bigint *bi_add_mag(const xt_bigint *a, const xt_bigint *b) {
  uint32_t count = a->length > b->length ? a->length : b->length;
  xt_bigint *r = bi_alloc(count + 1);
  uint64_t carry = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint64_t sum = carry;
    if (i < a->length) sum += a->limbs[i];
    if (i < b->length) sum += b->limbs[i];
    r->limbs[i] = (uint32_t)sum;
    carry = sum >> 32;
  }
  if (carry) r->limbs[count++] = (uint32_t)carry;
  r->length = count;
  r->sign = count ? 1 : 0;
  bi_normalize(r);
  return r;
}

/** `a - b` for magnitudes with `a >= b`. */
static xt_bigint *bi_sub_mag(const xt_bigint *a, const xt_bigint *b) {
  xt_bigint *r = bi_alloc(a->length);
  int64_t borrow = 0;
  for (uint32_t i = 0; i < a->length; i++) {
    int64_t cur = (int64_t)a->limbs[i] - borrow - (i < b->length ? (int64_t)b->limbs[i] : 0);
    if (cur < 0) {
      cur += 0x100000000LL;
      borrow = 1;
    } else {
      borrow = 0;
    }
    r->limbs[i] = (uint32_t)cur;
  }
  r->length = a->length;
  r->sign = 1;
  bi_normalize(r);
  return r;
}

static void bi_sub_mag_inplace(xt_bigint *a, const xt_bigint *b) {
  int64_t borrow = 0;
  for (uint32_t i = 0; i < a->length; i++) {
    int64_t cur = (int64_t)a->limbs[i] - borrow - (i < b->length ? (int64_t)b->limbs[i] : 0);
    if (cur < 0) {
      cur += 0x100000000LL;
      borrow = 1;
    } else {
      borrow = 0;
    }
    a->limbs[i] = (uint32_t)cur;
  }
  if (a->sign == 0) a->sign = 1;
  bi_normalize(a);
}

static xt_bigint *bi_mul_mag(const xt_bigint *a, const xt_bigint *b) {
  if (a->length == 0 || b->length == 0) return bi_zero();
  xt_bigint *r = bi_alloc(a->length + b->length);
  for (uint32_t i = 0; i < a->length; i++) {
    uint64_t carry = 0;
    for (uint32_t j = 0; j < b->length; j++) {
      uint64_t cur = (uint64_t)r->limbs[i + j] + (uint64_t)a->limbs[i] * b->limbs[j] + carry;
      r->limbs[i + j] = (uint32_t)cur;
      carry = cur >> 32;
    }
    uint32_t k = i + b->length;
    while (carry) {
      uint64_t cur = (uint64_t)r->limbs[k] + carry;
      r->limbs[k] = (uint32_t)cur;
      carry = cur >> 32;
      k++;
    }
  }
  r->length = a->length + b->length;
  r->sign = 1;
  bi_normalize(r);
  return r;
}

