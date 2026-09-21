/* -- division -------------------------------------------------------------- */

static void bi_divmod_mag(const xt_bigint *a, const xt_bigint *b, xt_bigint **qout, xt_bigint **rout) {
  if (b->length == 0) {
    bi_throw_range("Division by zero");
    *qout = bi_zero();
    *rout = bi_zero();
    return;
  }
  if (bi_cmp_mag(a, b) < 0) {
    *qout = bi_zero();
    *rout = bi_copy(a);
    if ((*rout)->length) (*rout)->sign = 1;
    return;
  }
  xt_bigint *q = bi_zero();
  xt_bigint *r = bi_zero();
  int32_t start = (int32_t)bi_bitlength(a) - 1;
  for (int32_t i = start; i >= 0; i--) {
    bi_shl1_inplace(r);
    if (bi_get_bit(a, (uint32_t)i)) {
      if (r->length == 0) {
        r->length = 1;
        r->limbs[0] = 1;
      } else {
        r->limbs[0] |= 1;
      }
      r->sign = 1;
    }
    if (bi_cmp_mag(r, b) >= 0) {
      bi_sub_mag_inplace(r, b);
      bi_set_bit(q, (uint32_t)i);
    }
  }
  *qout = q;
  *rout = r;
}

/* -- two's complement (bitwise operators) ---------------------------------- */

static void bi_to_twos(const xt_bigint *b, uint32_t *out, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) out[i] = i < b->length ? b->limbs[i] : 0;
  if (b->sign < 0) {
    uint64_t carry = 1;
    for (uint32_t i = 0; i < count; i++) {
      out[i] = ~out[i];
      uint64_t sum = (uint64_t)out[i] + carry;
      out[i] = (uint32_t)sum;
      carry = sum >> 32;
    }
  }
}

static xt_bigint *bi_from_twos(const uint32_t *in, uint32_t count) {
  xt_bigint *r = bi_alloc(count ? count : 1);
  if (count == 0) return r;
  if (!((in[count - 1] >> 31) & 1)) {
    memcpy(r->limbs, in, (size_t)count * sizeof(uint32_t));
    r->length = count;
    r->sign = 1;
    bi_normalize(r);
    return r;
  }
  uint64_t carry = 1;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t value = ~in[i];
    uint64_t sum = (uint64_t)value + carry;
    r->limbs[i] = (uint32_t)sum;
    carry = sum >> 32;
  }
  r->length = count;
  r->sign = -1;
  bi_normalize(r);
  return r;
}

static xt_bigint *bi_from_unsigned(const uint32_t *in, uint32_t count) {
  xt_bigint *r = bi_alloc(count ? count : 1);
  if (count) memcpy(r->limbs, in, (size_t)count * sizeof(uint32_t));
  r->length = count;
  r->sign = count ? 1 : 0;
  bi_normalize(r);
  return r;
}

