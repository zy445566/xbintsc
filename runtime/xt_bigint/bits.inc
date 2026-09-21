/* -- bit access ------------------------------------------------------------ */

static uint32_t bi_clz32(uint32_t x) {
  if (x == 0) return 32;
  uint32_t count = 0;
  while (!(x & 0x80000000u)) {
    x <<= 1;
    count++;
  }
  return count;
}

static uint32_t bi_bitlength(const xt_bigint *a) {
  if (a->length == 0) return 0;
  return (a->length - 1) * 32 + (32 - bi_clz32(a->limbs[a->length - 1]));
}

static int bi_get_bit(const xt_bigint *a, uint32_t index) {
  uint32_t limb = index >> 5;
  if (limb >= a->length) return 0;
  return (a->limbs[limb] >> (index & 31)) & 1;
}

static void bi_set_bit(xt_bigint *a, uint32_t index) {
  uint32_t limb = index >> 5;
  if (limb >= a->length) {
    bi_reserve(a, limb + 1);
    for (uint32_t i = a->length; i <= limb; i++) a->limbs[i] = 0;
    a->length = limb + 1;
  }
  a->limbs[limb] |= (uint32_t)1 << (index & 31);
  a->sign = 1;
}

/** Shift the magnitude left by one bit, preserving the (magnitude) sign. */
static void bi_shl1_inplace(xt_bigint *a) {
  if (a->length == 0) return;
  bi_reserve(a, a->length + 1);
  uint32_t carry = 0;
  for (uint32_t i = 0; i < a->length; i++) {
    uint32_t value = a->limbs[i];
    a->limbs[i] = (value << 1) | carry;
    carry = value >> 31;
  }
  if (carry) a->limbs[a->length++] = carry;
}

/* -- shifts ---------------------------------------------------------------- */

static xt_bigint *bi_shl_bits(const xt_bigint *a, uint32_t bits) {
  if (a->length == 0) return bi_zero();
  uint32_t word = bits >> 5;
  uint32_t shift = bits & 31;
  xt_bigint *r = bi_alloc(a->length + word + 1);
  if (shift == 0) {
    for (uint32_t i = 0; i < a->length; i++) r->limbs[i + word] = a->limbs[i];
    r->length = a->length + word;
  } else {
    uint32_t carry = 0;
    for (uint32_t i = 0; i < a->length; i++) {
      uint64_t cur = ((uint64_t)a->limbs[i] << shift) | carry;
      r->limbs[i + word] = (uint32_t)cur;
      carry = (uint32_t)(cur >> 32);
    }
    r->limbs[a->length + word] = carry;
    r->length = a->length + word + (carry ? 1 : 0);
  }
  r->sign = a->sign;
  bi_normalize(r);
  if (r->length) r->sign = a->sign;
  return r;
}

/** Logical right shift of a non-negative magnitude. */
static xt_bigint *bi_shr_bits(const xt_bigint *a, uint32_t bits) {
  if ((uint64_t)bits >= (uint64_t)a->length * 32) return bi_zero();
  uint32_t word = bits >> 5;
  uint32_t shift = bits & 31;
  uint32_t count = a->length - word;
  xt_bigint *r = bi_alloc(count);
  if (shift == 0) {
    for (uint32_t i = 0; i < count; i++) r->limbs[i] = a->limbs[i + word];
    r->length = count;
  } else {
    for (uint32_t i = 0; i < count; i++) {
      uint64_t low = (uint64_t)a->limbs[i + word] >> shift;
      uint64_t high = (i + word + 1 < a->length)
                          ? ((uint64_t)a->limbs[i + word + 1] << (32 - shift))
                          : 0;
      r->limbs[i] = (uint32_t)(low | high);
    }
    r->length = count;
  }
  r->sign = 1;
  bi_normalize(r);
  return r;
}

