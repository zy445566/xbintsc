/*
 * xbintsc runtime — arbitrary-precision integers (BigInt).
 *
 * BigInt values are sign-magnitude integers stored as little-endian base 2^32
 * limb arrays behind the `XT_TAG_BIGINT` NaN-box tag. The implementation is
 * deliberately small and dependency free: it favours clarity over throughput
 * and covers everything the language subset needs (literals, arithmetic,
 * bitwise operators, shifts, comparisons, string conversion and the
 * `BigInt.asIntN` / `BigInt.asUintN` statics).
 *
 * Division uses binary long division (shift/subtract). That is O(bits*limbs)
 * rather than the O(n*m) of Knuth's algorithm D, but keeps the code compact and
 * is more than fast enough for the fixed-width (64-bit) values the compiler
 * itself relies on.
 */

#include "rt_internal.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BI_LIMB_BITS 32
#define BI_MAX_SHIFT 0x01000000u /* 16 Mi bits: guard against runaway shifts */

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

/* ------------------------------------------------------------------------- */
/* Parsing and formatting                                                    */
/* ------------------------------------------------------------------------- */

static int bi_digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return -1;
}

xt_value xt_bigint_from_string(const char *data, size_t len) {
  size_t start = 0;
  size_t end = len;
  while (start < end && (data[start] == ' ' || data[start] == '\t' || data[start] == '\n' ||
                         data[start] == '\r' || data[start] == '\f' || data[start] == '\v'))
    start++;
  while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t' || data[end - 1] == '\n' ||
                         data[end - 1] == '\r' || data[end - 1] == '\f' || data[end - 1] == '\v'))
    end--;

  int sign = 1;
  if (start < end && (data[start] == '+' || data[start] == '-')) {
    if (data[start] == '-') sign = -1;
    start++;
  }

  int radix = 10;
  int hadPrefix = 0;
  if (start + 1 < end && data[start] == '0') {
    char marker = data[start + 1];
    if (marker == 'x' || marker == 'X') {
      radix = 16;
      start += 2;
      hadPrefix = 1;
    } else if (marker == 'o' || marker == 'O') {
      radix = 8;
      start += 2;
      hadPrefix = 1;
    } else if (marker == 'b' || marker == 'B') {
      radix = 2;
      start += 2;
      hadPrefix = 1;
    }
  }

  xt_bigint *r = bi_zero();
  size_t digits = 0;
  for (size_t i = start; i < end; i++) {
    char c = data[i];
    if (c == '_') continue;
    int digit = bi_digit_value(c);
    if (digit < 0 || digit >= radix) {
      bi_throw_syntax("Cannot convert to a BigInt");
      return bi_wrap(bi_zero());
    }
    bi_mul_small(r, (uint32_t)radix);
    bi_add_small(r, (uint32_t)digit);
    digits++;
  }

  if (digits == 0) {
    if (hadPrefix) {
      bi_throw_syntax("Cannot convert to a BigInt");
      return bi_wrap(bi_zero());
    }
    return bi_wrap(bi_zero());
  }
  r->sign = sign;
  bi_normalize(r);
  if (r->length) r->sign = sign;
  return bi_wrap(r);
}

static void bi_format_chunk(char *out, uint32_t value, int radix, int minDigits, int *written) {
  static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  char buffer[40];
  int count = 0;
  if (value == 0) {
    buffer[count++] = '0';
  } else {
    while (value > 0) {
      buffer[count++] = digits[value % (uint32_t)radix];
      value /= (uint32_t)radix;
    }
  }
  while (count < minDigits) buffer[count++] = '0';
  for (int i = 0; i < count; i++) out[i] = buffer[count - 1 - i];
  *written = count;
}

static xt_value bi_to_string_radix(const xt_bigint *b, int radix, int negative) {
  if (radix < 2 || radix > 36) {
    bi_throw_range("toString() radix must be between 2 and 36");
    return xt_string_from_cstr("0");
  }
  if (b->length == 0) return xt_string_from_cstr("0");

  uint32_t chunk = 1;
  int chunkDigits = 0;
  while ((uint64_t)chunk * (uint32_t)radix <= 0xffffffffULL) {
    chunk *= (uint32_t)radix;
    chunkDigits++;
  }

  xt_bigint *tmp = bi_copy(b);
  tmp->sign = 1;
  size_t capacity = 24;
  uint32_t *parts = (uint32_t *)malloc(capacity * sizeof(uint32_t));
  size_t count = 0;
  while (tmp->length > 0) {
    if (count + 1 > capacity) {
      capacity *= 2;
      parts = (uint32_t *)realloc(parts, capacity * sizeof(uint32_t));
    }
    parts[count++] = bi_divmod_small(tmp, chunk);
  }

  size_t maxChars = (size_t)count * (size_t)chunkDigits + 2;
  char *out = (char *)malloc(maxChars);
  size_t length = 0;
  if (negative) out[length++] = '-';
  int first = 1;
  for (size_t i = count; i-- > 0;) {
    int written = 0;
    bi_format_chunk(out + length, parts[i], radix, first ? 0 : chunkDigits, &written);
    length += (size_t)written;
    first = 0;
  }
  xt_value result = xt_string_new(out, length);
  free(parts);
  free(out);
  return result;
}

xt_value xt_bigint_to_string_radix(xt_value value, int radix) {
  const xt_bigint *b = BI(value);
  return bi_to_string_radix(b, radix, b->sign < 0);
}

xt_value xt_bigint_to_decimal(xt_value value) {
  const xt_bigint *b = BI(value);
  return bi_to_string_radix(b, 10, b->sign < 0);
}

/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */

double xt_bigint_to_double_value(xt_value value) {
  const xt_bigint *b = BI(value);
  double result = 0.0;
  for (uint32_t i = b->length; i-- > 0;) result = result * 4294967296.0 + (double)b->limbs[i];
  return b->sign < 0 ? -result : result;
}

static xt_bigint *bi_from_abs_double(double value) {
  xt_bigint *r = bi_alloc(8);
  uint32_t count = 0;
  while (value >= 1.0) {
    double remainder = fmod(value, 4294967296.0);
    if (count >= r->capacity) bi_reserve(r, r->capacity * 2);
    r->limbs[count++] = (uint32_t)remainder;
    value = floor(value / 4294967296.0);
  }
  r->length = count;
  r->sign = count ? 1 : 0;
  bi_normalize(r);
  return r;
}

xt_value xt_bigint_from_double(double value) {
  if (isnan(value) || isinf(value) || value != floor(value)) {
    bi_throw_range("The number cannot be converted to a BigInt because it is not an integer");
    return bi_wrap(bi_zero());
  }
  if (value == 0.0) return bi_wrap(bi_zero());
  int negative = value < 0;
  xt_bigint *r = bi_from_abs_double(negative ? -value : value);
  if (r->length) r->sign = negative ? -1 : 1;
  return bi_wrap(r);
}

static xt_value bi_from_xt_value(xt_value value) {
  if (XT_IS_BIGINT(value)) return value;
  if (XT_IS_NUMBER(value)) return xt_bigint_from_double(xt_to_double(value));
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    return xt_bigint_from_string(s->data, s->length);
  }
  if (value == XT_TRUE) return xt_bigint_from_i64(1);
  if (value == XT_FALSE) return xt_bigint_from_i64(0);
  bi_throw_type("Cannot convert value to a BigInt");
  return bi_wrap(bi_zero());
}

/* ------------------------------------------------------------------------- */
/* Comparisons                                                               */
/* ------------------------------------------------------------------------- */

static int bi_cmp(const xt_bigint *a, const xt_bigint *b) {
  if (a->sign != b->sign) return a->sign < b->sign ? -1 : 1;
  if (a->sign == 0) return 0;
  int cmp = bi_cmp_mag(a, b);
  return a->sign < 0 ? -cmp : cmp;
}

int xt_bigint_compare(xt_value a, xt_value b) { return bi_cmp(BI(a), BI(b)); }

int xt_bigint_compare_double(xt_value value, double other) {
  const xt_bigint *a = BI(value);
  if (isnan(other)) return 2;
  if (isinf(other)) return other > 0 ? -1 : 1;

  double truncated = trunc(other);
  int otherSign = truncated == 0.0 ? 0 : (truncated < 0 ? -1 : 1);
  xt_bigint *b = bi_from_abs_double(fabs(truncated));
  if (b->length) b->sign = otherSign;

  int cmp = bi_cmp(a, b);
  if (other != truncated && cmp == 0) return other > truncated ? -1 : 1;
  return cmp;
}

int xt_is_bigint(xt_value value) { return XT_IS_BIGINT(value); }

/* ------------------------------------------------------------------------- */
/* Arithmetic                                                                */
/* ------------------------------------------------------------------------- */

xt_value xt_bigint_neg(xt_value value) {
  xt_bigint *r = bi_copy(BI(value));
  r->sign = -r->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_add(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (a->sign == 0) return bv;
  if (b->sign == 0) return av;
  if (a->sign == b->sign) {
    xt_bigint *r = bi_add_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  int cmp = bi_cmp_mag(a, b);
  if (cmp == 0) return bi_wrap(bi_zero());
  if (cmp > 0) {
    xt_bigint *r = bi_sub_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  xt_bigint *r = bi_sub_mag(b, a);
  r->sign = b->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_sub(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) return av;
  if (a->sign == 0) return xt_bigint_neg(bv);
  if (a->sign != b->sign) {
    xt_bigint *r = bi_add_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  int cmp = bi_cmp_mag(a, b);
  if (cmp == 0) return bi_wrap(bi_zero());
  if (cmp > 0) {
    xt_bigint *r = bi_sub_mag(a, b);
    r->sign = a->sign;
    return bi_wrap(r);
  }
  xt_bigint *r = bi_sub_mag(b, a);
  r->sign = -a->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_mul(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (a->sign == 0 || b->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *r = bi_mul_mag(a, b);
  r->sign = a->sign * b->sign;
  return bi_wrap(r);
}

xt_value xt_bigint_div(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) {
    bi_throw_range("Division by zero");
    return bi_wrap(bi_zero());
  }
  if (a->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *q = NULL;
  xt_bigint *r = NULL;
  bi_divmod_mag(a, b, &q, &r);
  q->sign = q->length ? a->sign * b->sign : 0;
  return bi_wrap(q);
}

xt_value xt_bigint_mod(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign == 0) {
    bi_throw_range("Division by zero");
    return bi_wrap(bi_zero());
  }
  if (a->sign == 0) return bi_wrap(bi_zero());
  xt_bigint *q = NULL;
  xt_bigint *r = NULL;
  bi_divmod_mag(a, b, &q, &r);
  r->sign = r->length ? a->sign : 0;
  return bi_wrap(r);
}

xt_value xt_bigint_pow(xt_value av, xt_value bv) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  if (b->sign < 0) {
    bi_throw_range("Exponent must be non-negative");
    return bi_wrap(bi_zero());
  }
  if (b->sign == 0) return xt_bigint_from_i64(1);
  if (a->sign == 0) return bi_wrap(bi_zero());
  if (b->length > 2) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  uint64_t exponent = b->limbs[0];
  if (b->length == 2) exponent |= (uint64_t)b->limbs[1] << 32;
  if (exponent > 0xffffffffULL) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }

  xt_bigint *result = bi_from_u64(1, 1);
  xt_bigint *base = bi_copy(a);
  base->sign = 1;
  uint32_t exp = (uint32_t)exponent;
  while (exp > 0) {
    if (exp & 1) {
      xt_bigint *next = bi_mul_mag(result, base);
      next->sign = 1;
      result = next;
    }
    exp >>= 1;
    if (exp) {
      xt_bigint *next = bi_mul_mag(base, base);
      next->sign = 1;
      base = next;
    }
  }
  result->sign = (a->sign < 0 && (exponent & 1)) ? -1 : 1;
  if (result->length == 0) result->sign = 0;
  return bi_wrap(result);
}

/* ------------------------------------------------------------------------- */
/* Bitwise operators                                                         */
/* ------------------------------------------------------------------------- */

typedef enum { BI_AND, BI_OR, BI_XOR } bi_bitop;

static xt_value bi_bitwise(xt_value av, xt_value bv, bi_bitop op) {
  const xt_bigint *a = BI(av);
  const xt_bigint *b = BI(bv);
  uint32_t count = (a->length > b->length ? a->length : b->length) + 1;
  uint32_t *x = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
  uint32_t *y = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
  if (!x || !y) abort();
  bi_to_twos(a, x, count);
  bi_to_twos(b, y, count);
  for (uint32_t i = 0; i < count; i++) {
    if (op == BI_AND) x[i] &= y[i];
    else if (op == BI_OR) x[i] |= y[i];
    else x[i] ^= y[i];
  }
  xt_bigint *r = bi_from_twos(x, count);
  free(x);
  free(y);
  return bi_wrap(r);
}

xt_value xt_bigint_bit_and(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_AND); }
xt_value xt_bigint_bit_or(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_OR); }
xt_value xt_bigint_bit_xor(xt_value a, xt_value b) { return bi_bitwise(a, b, BI_XOR); }

xt_value xt_bigint_bit_not(xt_value value) {
  /* ~a === -a - 1. */
  xt_value negated = xt_bigint_neg(value);
  xt_value one = xt_bigint_from_i64(1);
  return xt_bigint_sub(negated, one);
}

static int64_t bi_shift_count(xt_value value, int *overflow) {
  const xt_bigint *b = BI(value);
  *overflow = 0;
  if (b->length == 0) return 0;
  if (b->length > 2) {
    *overflow = 1;
    return 0;
  }
  uint64_t magnitude = b->limbs[0];
  if (b->length == 2) magnitude |= (uint64_t)b->limbs[1] << 32;
  if (magnitude > (uint64_t)INT64_MAX) {
    *overflow = 1;
    return 0;
  }
  return b->sign < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
}

static xt_value bi_shl_count(xt_value value, uint32_t bits) {
  if (bits > BI_MAX_SHIFT) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  xt_bigint *r = bi_shl_bits(BI(value), bits);
  return bi_wrap(r);
}

static xt_value bi_shr_count(xt_value value, uint32_t bits) {
  const xt_bigint *a = BI(value);
  if (a->sign >= 0) {
    xt_bigint *r = bi_shr_bits(a, bits);
    if (r->length) r->sign = 1;
    return bi_wrap(r);
  }
  /* Arithmetic shift of a negative value: -( ((|a| - 1) >> bits) + 1 ). */
  xt_bigint *magnitude = bi_copy(a);
  magnitude->sign = 1;
  bi_sub_small(magnitude, 1);
  xt_bigint *shifted = bi_shr_bits(magnitude, bits);
  bi_add_small(shifted, 1);
  shifted->sign = -1;
  return bi_wrap(shifted);
}

xt_value xt_bigint_shl(xt_value av, xt_value bv) {
  int overflow = 0;
  int64_t count = bi_shift_count(bv, &overflow);
  if (overflow || count > (int64_t)BI_MAX_SHIFT) {
    bi_throw_range("Maximum BigInt size exceeded");
    return bi_wrap(bi_zero());
  }
  if (count < 0) return bi_shr_count(av, (uint32_t)(-count));
  return bi_shl_count(av, (uint32_t)count);
}

xt_value xt_bigint_shr(xt_value av, xt_value bv) {
  int overflow = 0;
  int64_t count = bi_shift_count(bv, &overflow);
  if (overflow || count > (int64_t)BI_MAX_SHIFT) {
    const xt_bigint *a = BI(av);
    if (a->sign < 0) return xt_bigint_from_i64(-1);
    return bi_wrap(bi_zero());
  }
  if (count < 0) return bi_shl_count(av, (uint32_t)(-count));
  return bi_shr_count(av, (uint32_t)count);
}

/* ------------------------------------------------------------------------- */
/* Public constructors and statics                                           */
/* ------------------------------------------------------------------------- */

xt_value xt_bigint_ctor(int32_t argc, xt_value *argv) {
  if (argc < 1) {
    bi_throw_type("Cannot convert undefined to a BigInt");
    return bi_wrap(bi_zero());
  }
  return bi_from_xt_value(argv[0]);
}

static xt_value bi_as_n(int32_t bits, xt_value value, int signedResult) {
  if (!XT_IS_BIGINT(value)) value = bi_from_xt_value(value);
  if (bits <= 0) return xt_bigint_from_i64(0);
  uint32_t count = ((uint32_t)bits + 31u) / 32u;
  uint32_t *tmp = (uint32_t *)calloc(count ? count : 1, sizeof(uint32_t));
  if (!tmp) abort();
  bi_to_twos(BI(value), tmp, count);
  uint32_t rem = (uint32_t)bits & 31u;
  if (rem) {
    uint32_t mask = (1u << rem) - 1u;
    tmp[count - 1] &= mask;
    if (signedResult && (tmp[count - 1] & (1u << (rem - 1)))) {
      tmp[count - 1] |= ~mask;
    }
  }
  xt_bigint *r = signedResult ? bi_from_twos(tmp, count) : bi_from_unsigned(tmp, count);
  free(tmp);
  return bi_wrap(r);
}

xt_value xt_bigint_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "asIntN") == 0) {
    int32_t bits = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    xt_value value = xt_arg_at(argc, argv, 1);
    return bi_as_n(bits, value, 1);
  }
  if (strcmp(fn, "asUintN") == 0) {
    int32_t bits = argc >= 1 ? xt_to_int32(argv[0]) : 0;
    xt_value value = xt_arg_at(argc, argv, 1);
    return bi_as_n(bits, value, 0);
  }
  return XT_UNDEFINED;
}

xt_value xt_bigint_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "toString") == 0) {
    int radix = argc >= 1 && argv[0] != XT_UNDEFINED ? xt_to_int32(argv[0]) : 10;
    return xt_bigint_to_string_radix(target, radix);
  }
  if (strcmp(method, "toLocaleString") == 0) return xt_bigint_to_decimal(target);
  if (strcmp(method, "valueOf") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}
