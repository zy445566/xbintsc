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

