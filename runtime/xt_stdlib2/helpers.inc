/*
 * `timegm` and `gmtime_r` are POSIX extensions that Windows' CRT does not
 * provide. MinGW-w64 (the bundled Windows toolchain) is affected too: its
 * <time.h> only exposes `gmtime_r` when `_POSIX_C_SOURCE` is set and has no
 * `timegm` at all, so wrap the MSVC-style equivalents for every `_WIN32` build.
 */
#if defined(_WIN32)
static time_t xt_timegm(struct tm *tm) { return (time_t)_mkgmtime64(tm); }
static struct tm *xt_gmtime_r(const time_t *timer, struct tm *buf) {
  return gmtime_s(buf, timer) == 0 ? buf : NULL;
}
static struct tm *xt_localtime_r(const time_t *timer, struct tm *buf) {
  return localtime_s(buf, timer) == 0 ? buf : NULL;
}
#else
static time_t xt_timegm(struct tm *tm) { return timegm(tm); }
static struct tm *xt_gmtime_r(const time_t *timer, struct tm *buf) { return gmtime_r(timer, buf); }
static struct tm *xt_localtime_r(const time_t *timer, struct tm *buf) { return localtime_r(timer, buf); }
#endif

int xt_value_equals(xt_value a, xt_value b) {
  if (XT_IS_NUMBER(a) && XT_IS_NUMBER(b)) {
    double x = xt_to_double(a);
    double y = xt_to_double(b);
    if (isnan(x) && isnan(y)) return 1;
    return x == y;
  }
  return xt_truthy(xt_seq(a, b));
}

static xt_value xt_number_to_radix(double value, int radix) {
  if (radix < 2 || radix > 36 || value != floor(value) || fabs(value) > 9.0e15) {
    return xt_to_string(xt_number(value));
  }
  int negative = value < 0;
  unsigned long long n = (unsigned long long)(negative ? -value : value);
  static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  char buffer[80];
  int pos = 0;
  if (n == 0) buffer[pos++] = '0';
  while (n > 0) { buffer[pos++] = digits[n % (unsigned)radix]; n /= (unsigned)radix; }
  char out[80];
  int o = 0;
  if (negative) out[o++] = '-';
  while (pos > 0) out[o++] = buffer[--pos];
  return xt_string_new(out, (size_t)o);
}
