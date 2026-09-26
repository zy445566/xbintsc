/* -- encoding helpers ----------------------------------------------------- */

static const char xt_fs_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Accepts a bare encoding string or an options object with `encoding`. */
static inline const char *xt_fs_encoding(xt_value options) {
  if (XT_IS_STRING(options)) return xt_string_data(options);
  if (XT_IS_OBJECT(options)) {
    xt_value encoding = xt_object_get_cstr(options, "encoding");
    if (XT_IS_STRING(encoding)) return xt_string_data(encoding);
  }
  return NULL;
}

static inline int xt_fs_is_hex(const char *encoding) {
  return encoding && strcmp(encoding, "hex") == 0;
}

static inline int xt_fs_is_base64(const char *encoding) {
  return encoding && strcmp(encoding, "base64") == 0;
}

static inline int xt_fs_is_base64url(const char *encoding) {
  return encoding && strcmp(encoding, "base64url") == 0;
}

static inline int xt_fs_is_utf16(const char *encoding) {
  return encoding && (strcmp(encoding, "utf16le") == 0 || strcmp(encoding, "ucs2") == 0 ||
                      strcmp(encoding, "ucs-2") == 0 || strcmp(encoding, "utf-16le") == 0);
}

static inline xt_value xt_fs_encode_hex(const unsigned char *data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  char *buffer = (char *)malloc(length * 2 + 1);
  if (!buffer) return xt_undefined();
  for (size_t i = 0; i < length; i++) {
    buffer[i * 2] = digits[data[i] >> 4];
    buffer[i * 2 + 1] = digits[data[i] & 0x0f];
  }
  xt_value result = xt_string_new(buffer, length * 2);
  free(buffer);
  return result;
}

static inline xt_value xt_fs_encode_base64(const unsigned char *data, size_t length) {
  size_t outLength = ((length + 2) / 3) * 4;
  char *buffer = (char *)malloc(outLength + 1);
  if (!buffer) return xt_undefined();
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_fs_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    uint32_t n = (uint32_t)data[i] << 16;
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = '=';
    buffer[j++] = '=';
  } else if (length - i == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = '=';
  }
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

/* Encode bytes for `readFileSync` according to `encoding` (default raw UTF-8). */
static inline xt_value xt_fs_encode_base64url(const unsigned char *data, size_t length) {
  size_t outLength = ((length + 2) / 3) * 4 + 1;
  char *buffer = (char *)malloc(outLength);
  if (!buffer) return xt_undefined();
  size_t i = 0, j = 0;
  while (i + 2 < length) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
    buffer[j++] = xt_fs_base64_alphabet[n & 63];
    i += 3;
  }
  if (length - i == 1) {
    uint32_t n = (uint32_t)data[i] << 16;
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
  } else if (length - i == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    buffer[j++] = xt_fs_base64_alphabet[(n >> 18) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 12) & 63];
    buffer[j++] = xt_fs_base64_alphabet[(n >> 6) & 63];
  }
  for (size_t k = 0; k < j; k++) {
    if (buffer[k] == '+') buffer[k] = '-';
    else if (buffer[k] == '/') buffer[k] = '_';
  }
  xt_value result = xt_string_new(buffer, j);
  free(buffer);
  return result;
}

static inline xt_value xt_fs_encode_bytes(const unsigned char *data, size_t length, const char *encoding) {
  if (xt_fs_is_hex(encoding)) return xt_fs_encode_hex(data, length);
  if (xt_fs_is_base64(encoding)) return xt_fs_encode_base64(data, length);
  if (xt_fs_is_base64url(encoding)) return xt_fs_encode_base64url(data, length);
  return xt_string_new((const char *)data, length);
}

static inline int xt_fs_base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

static inline int xt_fs_hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/*
 * Decode text for `writeFileSync` according to `encoding`. Returns a malloc'd
 * byte buffer (never NULL unless allocation failed) and stores its length in
 * `*outLength`; the caller frees it.
 */
static inline unsigned char *xt_fs_decode_text(const char *text, size_t length, const char *encoding, size_t *outLength) {
  unsigned char *bytes = NULL;
  if (xt_fs_is_base64(encoding) || xt_fs_is_base64url(encoding)) {
    size_t capacity = (length / 4 + 1) * 3;
    bytes = (unsigned char *)malloc(capacity + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int accumulator = 0, bits = 0;
    for (size_t i = 0; i < length; i++) {
      if (text[i] == '=') break;
      int value = xt_fs_base64_value(text[i]);
      if (value < 0) continue;
      accumulator = (accumulator << 6) | value;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        bytes[written++] = (unsigned char)((accumulator >> bits) & 0xff);
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  if (xt_fs_is_hex(encoding)) {
    bytes = (unsigned char *)malloc(length / 2 + 1);
    if (!bytes) return NULL;
    size_t written = 0;
    int high = -1;
    for (size_t i = 0; i < length; i++) {
      int value = xt_fs_hex_value(text[i]);
      if (value < 0) continue;
      if (high < 0) {
        high = value;
      } else {
        bytes[written++] = (unsigned char)((high << 4) | value);
        high = -1;
      }
    }
    bytes[written] = '\0';
    *outLength = written;
    return bytes;
  }
  bytes = (unsigned char *)malloc(length + 1);
  if (!bytes) return NULL;
  if (length > 0) memcpy(bytes, text, length);
  bytes[length] = '\0';
  *outLength = length;
  return bytes;
}
