/*
 * ECMAScript number formatting.
 *
 * A double is rendered as the shortest decimal string that round-trips, then
 * laid out with JavaScript's notation rules: fixed notation unless the decimal
 * exponent is < -6 or >= 21, and a bare (unpadded, explicitly signed) exponent.
 * The `toFixed`/`toExponential`/`toPrecision` variants reuse the same renderer.
 */
static void xt_render_decimal(int negative, const char *digits, int nd, int exp10, int expo, char *buffer, size_t size) {
  size_t o = 0;
  if (negative && o + 1 < size) buffer[o++] = '-';
  if (expo) {
    if (o + 1 < size) buffer[o++] = digits[0];
    if (nd > 1) {
      if (o + 1 < size) buffer[o++] = '.';
      for (int i = 1; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
    }
    int written = snprintf(buffer + o, size - o, "e%c%d", exp10 < 0 ? '-' : '+', exp10 < 0 ? -exp10 : exp10);
    if (written > 0) o += (size_t)written;
  } else if (exp10 >= 0) {
    int intDigits = exp10 + 1;
    if (nd <= intDigits) {
      for (int i = 0; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
      for (int i = nd; i < intDigits && o + 1 < size; i++) buffer[o++] = '0';
    } else {
      for (int i = 0; i < intDigits && o + 1 < size; i++) buffer[o++] = digits[i];
      if (o + 1 < size) buffer[o++] = '.';
      for (int i = intDigits; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
    }
  } else {
    if (o + 1 < size) buffer[o++] = '0';
    if (o + 1 < size) buffer[o++] = '.';
    for (int i = 0; i < -exp10 - 1 && o + 1 < size; i++) buffer[o++] = '0';
    for (int i = 0; i < nd && o + 1 < size; i++) buffer[o++] = digits[i];
  }
  buffer[o] = '\0';
}

/* Split a `%e`-formatted string into its significant digits and exponent. */
static void xt_parse_scientific(const char *sci, char *digits, int *nd, int *exp10) {
  int n = 0;
  *exp10 = 0;
  const char *q = sci;
  while (*q && *q != 'e' && *q != 'E') {
    if (*q >= '0' && *q <= '9') digits[n++] = *q;
    q++;
  }
  if (*q == 'e' || *q == 'E') *exp10 = atoi(q + 1);
  *nd = n;
}

void xt_number_to_js_string(double d, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (d == 0.0) {
    snprintf(buffer, size, "0");
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[64];
  for (int p = 1; p <= 17; p++) {
    snprintf(sci, sizeof(sci), "%.*e", p - 1, x);
    if (strtod(sci, NULL) == x) break;
  }
  char digits[32];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  while (nd > 1 && digits[nd - 1] == '0') nd--;
  xt_render_decimal(negative, digits, nd, exp10, exp10 < -6 || exp10 >= 21, buffer, size);
}

void xt_number_to_exponential(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[64];
  if (precision < 0) {
    /* No fractionDigits argument: use the shortest round-tripping form. */
    for (int p = 1; p <= 17; p++) {
      snprintf(sci, sizeof(sci), "%.*e", p - 1, x);
      if (strtod(sci, NULL) == x) break;
    }
  } else {
    snprintf(sci, sizeof(sci), "%.*e", precision, x);
  }
  char digits[64];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  if (precision < 0) while (nd > 1 && digits[nd - 1] == '0') nd--;
  xt_render_decimal(negative, digits, nd, exp10, 1, buffer, size);
}

void xt_number_to_precision(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  char sci[512];
  snprintf(sci, sizeof(sci), "%.*e", precision - 1, x);
  char digits[512];
  int nd = 0;
  int exp10 = 0;
  xt_parse_scientific(sci, digits, &nd, &exp10);
  xt_render_decimal(negative, digits, nd, exp10, exp10 < -6 || exp10 >= precision, buffer, size);
}

void xt_number_to_fixed(double d, int precision, char *buffer, size_t size) {
  if (isnan(d)) {
    snprintf(buffer, size, "NaN");
    return;
  }
  if (isinf(d)) {
    snprintf(buffer, size, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (fabs(d) >= 1e21) {
    xt_number_to_js_string(d, buffer, size);
    return;
  }
  int negative = d < 0;
  double x = negative ? -d : d;
  /* 20 guard digits let us round exactly half away from zero, which is what
     ECMAScript specifies and what `printf`'s round-half-to-even does not. */
  char raw[1024];
  snprintf(raw, sizeof(raw), "%.*f", precision + 20, x);
  const char *dot = strchr(raw, '.');
  int intLen = dot ? (int)(dot - raw) : (int)strlen(raw);
  int len = intLen;
  if (precision > 0) len += 1 + precision;
  if (len >= (int)sizeof(raw)) len = (int)sizeof(raw) - 1;
  char work[1024];
  memcpy(work, raw, (size_t)len);
  work[len] = '\0';
  /* The digit that decides rounding is the first one we dropped. With a
     precision of zero that is the character after the decimal point. */
  int firstDropped = precision > 0 ? len : (dot ? intLen + 1 : len);
  if (dot && raw[firstDropped] >= '5') {
    int i = len - 1;
    while (i >= 0) {
      if (work[i] == '.') {
        i--;
        continue;
      }
      if (work[i] < '9') {
        work[i]++;
        break;
      }
      work[i] = '0';
      i--;
    }
    if (i < 0) {
      memmove(work + 1, work, strlen(work) + 1);
      work[0] = '1';
    }
  }
  if (negative) snprintf(buffer, size, "-%s", work);
  else snprintf(buffer, size, "%s", work);
}
