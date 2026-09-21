/* -- small in-place arithmetic -------------------------------------------- */

static void bi_add_small(xt_bigint *b, uint32_t value) {
  if (value == 0) return;
  bi_reserve(b, b->length + 1);
  uint64_t carry = value;
  for (uint32_t i = 0; i < b->length && carry; i++) {
    uint64_t sum = (uint64_t)b->limbs[i] + carry;
    b->limbs[i] = (uint32_t)sum;
    carry = sum >> 32;
  }
  if (carry) b->limbs[b->length++] = (uint32_t)carry;
  b->sign = 1;
}

static void bi_sub_small(xt_bigint *b, uint32_t value) {
  uint64_t borrow = value;
  for (uint32_t i = 0; i < b->length && borrow; i++) {
    uint64_t cur = b->limbs[i];
    if (cur < borrow) {
      b->limbs[i] = (uint32_t)(cur + 0x100000000ULL - borrow);
      borrow = 1;
    } else {
      b->limbs[i] = (uint32_t)(cur - borrow);
      borrow = 0;
    }
  }
  bi_normalize(b);
}

static void bi_mul_small(xt_bigint *b, uint32_t value) {
  if (b->length == 0) return;
  if (value == 0) {
    b->length = 0;
    b->sign = 0;
    return;
  }
  bi_reserve(b, b->length + 1);
  uint64_t carry = 0;
  for (uint32_t i = 0; i < b->length; i++) {
    uint64_t product = (uint64_t)b->limbs[i] * value + carry;
    b->limbs[i] = (uint32_t)product;
    carry = product >> 32;
  }
  if (carry) b->limbs[b->length++] = (uint32_t)carry;
}

static uint32_t bi_divmod_small(xt_bigint *b, uint32_t divisor) {
  uint64_t remainder = 0;
  for (uint32_t i = b->length; i-- > 0;) {
    uint64_t cur = (remainder << 32) | b->limbs[i];
    b->limbs[i] = (uint32_t)(cur / divisor);
    remainder = cur % divisor;
  }
  bi_normalize(b);
  return (uint32_t)remainder;
}

