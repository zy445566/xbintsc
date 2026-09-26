/* -- small helpers -------------------------------------------------------- */

static void xt_re_set_bit(unsigned char *bits, unsigned char c) {
  bits[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static int xt_re_bit_test(const unsigned char *bits, unsigned char c) {
  return (bits[c >> 3] >> (c & 7)) & 1;
}

static unsigned char xt_re_lower(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 'a');
  return c;
}

static void xt_re_add_range(unsigned char *bits, unsigned char lo, unsigned char hi) {
  unsigned int c;
  if (hi < lo) {
    unsigned char t = lo;
    lo = hi;
    hi = t;
  }
  for (c = lo; c <= hi; c++) xt_re_set_bit(bits, (unsigned char)c);
}

/* Adds a predefined class such as \d or \w. Returns 1 when handled. */
static int xt_re_add_predefined(unsigned char *bits, char e) {
  int c;
  switch (e) {
    case 'd':
      xt_re_add_range(bits, '0', '9');
      return 1;
    case 'D':
      for (c = 0; c < 256; c++)
        if (!(c >= '0' && c <= '9')) xt_re_set_bit(bits, (unsigned char)c);
      return 1;
    case 'w':
      xt_re_add_range(bits, 'a', 'z');
      xt_re_add_range(bits, 'A', 'Z');
      xt_re_add_range(bits, '0', '9');
      xt_re_set_bit(bits, '_');
      return 1;
    case 'W':
      for (c = 0; c < 256; c++) {
        int is_word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
        if (!is_word) xt_re_set_bit(bits, (unsigned char)c);
      }
      return 1;
    case 's':
      xt_re_set_bit(bits, ' ');
      xt_re_set_bit(bits, '\t');
      xt_re_set_bit(bits, '\n');
      xt_re_set_bit(bits, '\r');
      xt_re_set_bit(bits, '\f');
      xt_re_set_bit(bits, '\v');
      return 1;
    case 'S':
      for (c = 0; c < 256; c++) {
        int is_space = c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                       c == '\f' || c == '\v';
        if (!is_space) xt_re_set_bit(bits, (unsigned char)c);
      }
      return 1;
    default:
      return 0;
  }
}
