/*
 * Node.js `querystring` module for xbintsc.
 *
 * Implements `parse` / `stringify` (and their `decode` / `encode` aliases)
 * plus `escape` / `unescape`. Percent-encoding follows
 * `encodeURIComponent`/`decodeURIComponent` semantics and, as Node does for
 * form data, `+` decodes to a space.
 */

#include "../node_common.h"

/* -- growable string buffer ---------------------------------------------- */

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} qs_buf;

static void qs_buf_init(qs_buf *buf) {
  buf->capacity = 64;
  buf->length = 0;
  buf->data = (char *)malloc(buf->capacity);
  if (buf->data) buf->data[0] = '\0';
}

static void qs_buf_putc(qs_buf *buf, char c) {
  if (!buf->data) return;
  if (buf->length + 2 > buf->capacity) {
    while (buf->length + 2 > buf->capacity) buf->capacity *= 2;
    buf->data = (char *)realloc(buf->data, buf->capacity);
    if (!buf->data) return;
  }
  buf->data[buf->length++] = c;
  buf->data[buf->length] = '\0';
}

static void qs_buf_puts(qs_buf *buf, const char *text, size_t length) {
  for (size_t i = 0; i < length; i++) qs_buf_putc(buf, text[i]);
}

static xt_value qs_buf_finish(qs_buf *buf) {
  xt_value result = buf->data ? xt_string_new(buf->data, buf->length) : xt_string_from_cstr("");
  free(buf->data);
  buf->data = NULL;
  return result;
}

/* -- percent encoding ----------------------------------------------------- */

static int qs_unreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
}

static int qs_hex(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* encodeURIComponent-style escaping of `length` bytes. */
static xt_value qs_escape_bytes(const char *text, size_t length) {
  static const char digits[] = "0123456789ABCDEF";
  qs_buf buf;
  qs_buf_init(&buf);
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (qs_unreserved(c)) {
      qs_buf_putc(&buf, (char)c);
    } else {
      qs_buf_putc(&buf, '%');
      qs_buf_putc(&buf, digits[c >> 4]);
      qs_buf_putc(&buf, digits[c & 0x0f]);
    }
  }
  return qs_buf_finish(&buf);
}

/* decodeURIComponent-style unescaping, with `+` meaning space. */
static xt_value qs_unescape_bytes(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (!out) return xt_string_from_cstr("");
  size_t written = 0;
  for (size_t i = 0; i < length; i++) {
    char c = text[i];
    if (c == '+') {
      out[written++] = ' ';
    } else if (c == '%' && i + 2 < length && qs_hex(text[i + 1]) >= 0 && qs_hex(text[i + 2]) >= 0) {
      out[written++] = (char)((qs_hex(text[i + 1]) << 4) | qs_hex(text[i + 2]));
      i += 2;
    } else {
      out[written++] = c;
    }
  }
  out[written] = '\0';
  xt_value result = xt_string_new(out, written);
  free(out);
  return result;
}

/* -- options -------------------------------------------------------------- */

static const char *qs_option(int32_t argc, xt_value *argv, int32_t index, const char *fallback) {
  if (index >= argc) return fallback;
  xt_value value = argv[index];
  if (XT_IS_STRING(value)) {
    const char *data = xt_string_data(value);
    return data ? data : fallback;
  }
  return fallback;
}

/* -- parse ---------------------------------------------------------------- */

static void qs_assign(xt_value result, xt_value key, xt_value value) {
  if (!xt_truthy(xt_object_has_own(result, key))) {
    xt_object_set(result, key, value);
    return;
  }
  xt_value existing = xt_object_get(result, key);
  if (XT_IS_ARRAY(existing)) {
    xt_array_push(existing, value);
    return;
  }
  xt_value array = xt_array_new(1, &existing);
  xt_array_push(array, value);
  xt_object_set(result, key, array);
}

static void qs_parse_pair(xt_value result, const char *pair, size_t length, const char *eq, size_t eqLength) {
  size_t eqIndex = length;
  if (eqLength > 0) {
    for (size_t i = 0; i + eqLength <= length; i++) {
      if (memcmp(pair + i, eq, eqLength) == 0) {
        eqIndex = i;
        break;
      }
    }
  }
  size_t keyLength = eqIndex;
  const char *valueStart = pair + length;
  size_t valueLength = 0;
  if (eqIndex < length) {
    valueStart = pair + eqIndex + eqLength;
    valueLength = length - eqIndex - eqLength;
  }
  xt_value key = qs_unescape_bytes(pair, keyLength);
  xt_value value = qs_unescape_bytes(valueStart, valueLength);
  qs_assign(result, key, value);
}

xt_value xt_querystring_parse(int32_t argc, xt_value *argv) {
  xt_value result = xt_object_new();
  xt_value input = xt_arg(argc, argv, 0);
  if (!XT_IS_STRING(input)) return result;
  const char *text = xt_string_data(input);
  size_t length = (size_t)xt_string_length_value(input);
  const char *sep = qs_option(argc, argv, 1, "&");
  const char *eq = qs_option(argc, argv, 2, "=");
  size_t sepLength = strlen(sep);
  size_t eqLength = strlen(eq);
  size_t start = 0;
  while (start <= length) {
    size_t end = start;
    while (end < length && !(sepLength > 0 && end + sepLength <= length && memcmp(text + end, sep, sepLength) == 0)) {
      end++;
    }
    qs_parse_pair(result, text + start, end - start, eq, eqLength);
    if (end >= length) break;
    start = end + sepLength;
  }
  return result;
}

/* -- stringify ------------------------------------------------------------ */

static void qs_stringify_scalar(qs_buf *buf, xt_value key, xt_value value, const char *eq) {
  const char *keyData = xt_string_data(key);
  size_t keyLength = (size_t)xt_string_length_value(key);
  xt_value escapedKey = qs_escape_bytes(keyData, keyLength);
  qs_buf_puts(buf, xt_string_data(escapedKey), (size_t)xt_string_length_value(escapedKey));
  qs_buf_puts(buf, eq, strlen(eq));

  xt_value text = value;
  if (xt_truthy(xt_is_nullish(value))) {
    return; /* null / undefined stringify to the empty string */
  }
  if (!XT_IS_STRING(text)) text = xt_to_string(text);
  const char *data = xt_string_data(text);
  size_t length = (size_t)xt_string_length_value(text);
  xt_value escaped = qs_escape_bytes(data, length);
  qs_buf_puts(buf, xt_string_data(escaped), (size_t)xt_string_length_value(escaped));
}

xt_value xt_querystring_stringify(int32_t argc, xt_value *argv) {
  xt_value object = xt_arg(argc, argv, 0);
  if (!XT_IS_OBJECT(object) || XT_IS_ARRAY(object)) return xt_string_from_cstr("");
  const char *sep = qs_option(argc, argv, 1, "&");
  const char *eq = qs_option(argc, argv, 2, "=");

  qs_buf buf;
  qs_buf_init(&buf);
  xt_value keys = xt_object_keys(object);
  int32_t count = (int32_t)xt_to_number(xt_array_length(keys));
  int first = 1;
  for (int32_t i = 0; i < count; i++) {
    xt_value key = xt_array_get(keys, xt_number((double)i));
    xt_value value = xt_object_get(object, key);
    if (XT_IS_ARRAY(value)) {
      int32_t items = (int32_t)xt_to_number(xt_array_length(value));
      for (int32_t j = 0; j < items; j++) {
        if (!first) qs_buf_puts(&buf, sep, strlen(sep));
        first = 0;
        qs_stringify_scalar(&buf, key, xt_array_get(value, xt_number((double)j)), eq);
      }
    } else {
      if (!first) qs_buf_puts(&buf, sep, strlen(sep));
      first = 0;
      qs_stringify_scalar(&buf, key, value, eq);
    }
  }
  return qs_buf_finish(&buf);
}

/* -- escape / unescape ---------------------------------------------------- */

xt_value xt_querystring_escape(int32_t argc, xt_value *argv) {
  xt_value text = xt_arg(argc, argv, 0);
  if (!XT_IS_STRING(text)) text = xt_to_string(text);
  return qs_escape_bytes(xt_string_data(text), (size_t)xt_string_length_value(text));
}

xt_value xt_querystring_unescape(int32_t argc, xt_value *argv) {
  xt_value text = xt_arg(argc, argv, 0);
  if (!XT_IS_STRING(text)) text = xt_to_string(text);
  return qs_unescape_bytes(xt_string_data(text), (size_t)xt_string_length_value(text));
}

/* -- namespace dispatcher ------------------------------------------------- */

xt_value xt_querystring_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "parse") == 0 || strcmp(fn, "decode") == 0) return xt_querystring_parse(argc, argv);
  if (strcmp(fn, "stringify") == 0 || strcmp(fn, "encode") == 0) return xt_querystring_stringify(argc, argv);
  if (strcmp(fn, "escape") == 0) return xt_querystring_escape(argc, argv);
  if (strcmp(fn, "unescape") == 0) return xt_querystring_unescape(argc, argv);
  return xt_undefined();
}
