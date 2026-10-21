/* ------------------------------------------------------------------------- */
/* UTF-8 storage, UTF-16 view                                                */
/* ------------------------------------------------------------------------- */
/*
 * Strings are stored as UTF-8, but JavaScript observes them as sequences of
 * UTF-16 code units. The helpers here bridge the two views:
 *
 *   - a decoded code point above 0xFFFF occupies two UTF-16 units;
 *   - a surrogate half that is not part of a pair (produced by indexing or
 *     slicing through a pair) is stored as its own three-byte WTF-8 sequence;
 *   - `xt_utf8_normalize_surrogates` recombines adjacent WTF-8 halves so that
 *     concatenation and flattening keep the canonical four-byte form.
 *
 * Invalid UTF-8 bytes decode as U+FFFD with width 1, so a C extension that
 * hands over arbitrary bytes can never make the walkers run off the end.
 */

uint32_t xt_utf8_decode(const char *data, size_t length, size_t *width) {
  const unsigned char *bytes = (const unsigned char *)data;
  if (length == 0) {
    if (width) *width = 0;
    return 0xFFFD;
  }
  unsigned char first = bytes[0];
  if (first < 0x80) {
    if (width) *width = 1;
    return first;
  }
  if ((first & 0xE0) == 0xC0 && length >= 2 && (bytes[1] & 0xC0) == 0x80) {
    if (width) *width = 2;
    return ((uint32_t)(first & 0x1F) << 6) | (uint32_t)(bytes[1] & 0x3F);
  }
  if ((first & 0xF0) == 0xE0 && length >= 3 && (bytes[1] & 0xC0) == 0x80 &&
      (bytes[2] & 0xC0) == 0x80) {
    if (width) *width = 3;
    return ((uint32_t)(first & 0x0F) << 12) | ((uint32_t)(bytes[1] & 0x3F) << 6) |
           (uint32_t)(bytes[2] & 0x3F);
  }
  if ((first & 0xF8) == 0xF0 && length >= 4 && (bytes[1] & 0xC0) == 0x80 &&
      (bytes[2] & 0xC0) == 0x80 && (bytes[3] & 0xC0) == 0x80) {
    uint32_t code = ((uint32_t)(first & 0x07) << 18) | ((uint32_t)(bytes[1] & 0x3F) << 12) |
                    ((uint32_t)(bytes[2] & 0x3F) << 6) | (uint32_t)(bytes[3] & 0x3F);
    if (code >= 0x10000 && code <= 0x10FFFF) {
      if (width) *width = 4;
      return code;
    }
  }
  /* Invalid or overlong sequence: consume one byte, report the replacement. */
  if (width) *width = 1;
  return 0xFFFD;
}

size_t xt_utf8_encode(uint32_t code, char *out) {
  if (code < 0x80) {
    out[0] = (char)code;
    return 1;
  }
  if (code < 0x800) {
    out[0] = (char)(0xC0 | (code >> 6));
    out[1] = (char)(0x80 | (code & 0x3F));
    return 2;
  }
  if (code < 0x10000) {
    out[0] = (char)(0xE0 | (code >> 12));
    out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[2] = (char)(0x80 | (code & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | ((code >> 18) & 0x07));
  out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
  out[3] = (char)(0x80 | (code & 0x3F));
  return 4;
}

void xt_utf8_measure(const char *data, size_t length, uint32_t *units, uint32_t *points) {
  size_t offset = 0;
  uint32_t unitCount = 0;
  uint32_t pointCount = 0;
  while (offset < length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(data + offset, length - offset, &width);
    if (width == 0) break;
    unitCount += code >= 0x10000 ? 2 : 1;
    pointCount++;
    offset += width;
  }
  if (units) *units = unitCount;
  if (points) *points = pointCount;
}

uint32_t xt_utf8_utf16_length(const char *data, size_t length) {
  uint32_t units = 0;
  xt_utf8_measure(data, length, &units, NULL);
  return units;
}

/* Both halves of a surrogate pair are encoded as three-byte WTF-8 sequences:
 * `ED A0..AF xx` for D800-DBFF and `ED B0..BF xx` for DC00-DFFF. */
static int xt_wtf8_surrogate(const unsigned char *bytes, size_t length, uint32_t *code, int *high) {
  if (length < 3 || bytes[0] != 0xED || bytes[1] < 0xA0 || bytes[1] > 0xBF ||
      bytes[2] < 0x80 || bytes[2] > 0xBF)
    return 0;
  *code = ((uint32_t)(bytes[0] & 0x0F) << 12) | ((uint32_t)(bytes[1] & 0x3F) << 6) |
          (uint32_t)(bytes[2] & 0x3F);
  *high = bytes[1] <= 0xAF;
  return 1;
}

size_t xt_utf8_normalize_surrogates(char *data, size_t length) {
  const unsigned char *source = (const unsigned char *)data;
  unsigned char *write = (unsigned char *)data;
  size_t offset = 0;
  while (offset < length) {
    uint32_t highCode = 0;
    int high = 0;
    if (xt_wtf8_surrogate(source + offset, length - offset, &highCode, &high) && high) {
      uint32_t lowCode = 0;
      int low = 0;
      if (offset + 3 < length &&
          xt_wtf8_surrogate(source + offset + 3, length - offset - 3, &lowCode, &low) && !low) {
        uint32_t code = 0x10000 + ((highCode - 0xD800) << 10) + (lowCode - 0xDC00);
        write += xt_utf8_encode(code, (char *)write);
        offset += 6;
        continue;
      }
    }
    *write++ = source[offset++];
  }
  return (size_t)(write - (unsigned char *)data);
}

void xt_string_finish(xt_string *s) {
  if (!s || !s->flat) return;
  size_t length = xt_utf8_normalize_surrogates(s->flat, s->length);
  s->length = (uint32_t)length;
  s->flat[length] = '\0';
  xt_utf8_measure(s->flat, length, &s->utf16_length, &s->code_point_count);
}

uint32_t xt_string_utf16_length(xt_string *s) {
  if (!s || s->length == 0) return 0;
  if (s->utf16_length) return s->utf16_length;
  if (!s->flat) xt_string_bytes(s);
  else xt_utf8_measure(s->flat, s->length, &s->utf16_length, &s->code_point_count);
  return s->utf16_length;
}

uint32_t xt_string_code_point_count(xt_string *s) {
  if (!s || s->length == 0) return 0;
  if (s->code_point_count) return s->code_point_count;
  if (!s->flat) xt_string_bytes(s);
  else xt_utf8_measure(s->flat, s->length, &s->utf16_length, &s->code_point_count);
  return s->code_point_count;
}

/* A string whose byte and UTF-16 lengths agree contains only ASCII, so the two
 * index spaces coincide and every walker below can skip decoding. */
static int xt_string_is_ascii(xt_string *s) {
  return s->length == xt_string_utf16_length(s);
}

int32_t xt_string_code_unit(xt_string *s, int32_t index) {
  if (!s || index < 0 || (uint32_t)index >= s->length) return -1;
  if (xt_string_is_ascii(s)) return (int32_t)(unsigned char)xt_string_bytes(s)[index];
  const char *bytes = xt_string_bytes(s);
  size_t offset = 0;
  int32_t unit = 0;
  while (offset < s->length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    if (code >= 0x10000) {
      uint32_t value = code - 0x10000;
      if (unit == index) return (int32_t)(0xD800 + (value >> 10));
      if (unit + 1 == index) return (int32_t)(0xDC00 + (value & 0x3FF));
      unit += 2;
    } else {
      if (unit == index) return (int32_t)code;
      unit += 1;
    }
    offset += width;
  }
  return -1;
}

int32_t xt_string_code_point_at(xt_string *s, int32_t index) {
  if (!s || index < 0) return -1;
  if (xt_string_is_ascii(s)) return index < (int32_t)s->length ? (int32_t)(unsigned char)xt_string_bytes(s)[index] : -1;
  const char *bytes = xt_string_bytes(s);
  size_t offset = 0;
  int32_t unit = 0;
  while (offset < s->length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    int widthUnits = code >= 0x10000 ? 2 : 1;
    if (unit == index) return (int32_t)code;
    if (widthUnits == 2 && unit + 1 == index)
      return (int32_t)(0xDC00 + ((code - 0x10000) & 0x3FF));
    unit += widthUnits;
    offset += width;
  }
  return -1;
}

xt_value xt_string_from_code_point(uint32_t code) {
  char buffer[4];
  size_t length = xt_utf8_encode(code, buffer);
  return xt_string_new(buffer, length);
}

xt_value xt_string_from_units(const uint16_t *units, size_t count) {
  size_t capacity = count * 4 + 1;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t length = 0;
  for (size_t i = 0; i < count; i++) {
    uint32_t code = units[i];
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < count && units[i + 1] >= 0xDC00 &&
        units[i + 1] <= 0xDFFF) {
      code = 0x10000 + ((code - 0xD800) << 10) + ((uint32_t)units[i + 1] - 0xDC00);
      i++;
    }
    length += xt_utf8_encode(code, out + length);
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

xt_value xt_string_unit_value(xt_string *s, int32_t index) {
  int32_t unit = xt_string_code_unit(s, index);
  if (unit < 0) return xt_string_from_cstr("");
  return xt_string_from_code_point((uint32_t)unit);
}

xt_value xt_string_code_point_value(xt_string *s, int32_t index) {
  if (!s || index < 0) return XT_UNDEFINED;
  if (xt_string_is_ascii(s))
    return index < (int32_t)s->length
               ? xt_string_from_code_point((uint32_t)(unsigned char)xt_string_bytes(s)[index])
               : XT_UNDEFINED;
  const char *bytes = xt_string_bytes(s);
  size_t offset = 0;
  int32_t point = 0;
  while (offset < s->length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    if (point == index) return xt_string_from_code_point(code);
    point++;
    offset += width;
  }
  return XT_UNDEFINED;
}

xt_value xt_string_slice_units(xt_string *s, int32_t start, int32_t end) {
  if (!s) return xt_string_from_cstr("");
  int32_t units = (int32_t)xt_string_utf16_length(s);
  if (start < 0) start = 0;
  if (start > units) start = units;
  if (end < 0) end = 0;
  if (end > units) end = units;
  if (start >= end) return xt_string_from_cstr("");
  if (xt_string_is_ascii(s)) {
    const char *ascii = xt_string_bytes(s);
    return xt_string_new(ascii + start, (size_t)(end - start));
  }

  const char *bytes = xt_string_bytes(s);
  /* Splitting a surrogate pair expands four bytes into two three-byte WTF-8
   * halves, so leave two bytes of headroom. */
  char *out = (char *)malloc((size_t)s->length + 8);
  if (!out) abort();
  size_t length = 0;
  size_t offset = 0;
  int32_t unit = 0;
  while (offset < s->length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    if (code >= 0x10000) {
      uint32_t value = code - 0x10000;
      if (start <= unit && end >= unit + 2) {
        length += xt_utf8_encode(code, out + length);
      } else if (start <= unit && end == unit + 1) {
        length += xt_utf8_encode(0xD800 + (value >> 10), out + length);
      } else if (start == unit + 1 && end >= unit + 2) {
        length += xt_utf8_encode(0xDC00 + (value & 0x3FF), out + length);
      }
      unit += 2;
    } else {
      if (unit >= start && unit < end) length += xt_utf8_encode(code, out + length);
      unit += 1;
    }
    offset += width;
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

int32_t xt_string_byte_offset(xt_string *s, int32_t unit_index) {
  if (!s || unit_index <= 0) return 0;
  if (xt_string_is_ascii(s)) return unit_index < (int32_t)s->length ? unit_index : (int32_t)s->length;
  const char *bytes = xt_string_bytes(s);
  size_t offset = 0;
  int32_t unit = 0;
  while (offset < s->length) {
    if (unit >= unit_index) return (int32_t)offset;
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    int widthUnits = code >= 0x10000 ? 2 : 1;
    if (widthUnits == 2 && unit + 1 == unit_index) return (int32_t)offset;
    unit += widthUnits;
    offset += width;
  }
  return (int32_t)s->length;
}

int32_t xt_string_unit_offset(xt_string *s, int32_t byte_offset) {
  if (!s || byte_offset <= 0) return 0;
  if ((uint32_t)byte_offset >= s->length) return (int32_t)xt_string_utf16_length(s);
  if (xt_string_is_ascii(s)) return byte_offset;
  const char *bytes = xt_string_bytes(s);
  size_t offset = 0;
  int32_t unit = 0;
  while (offset < (size_t)byte_offset && offset < s->length) {
    size_t width = 0;
    uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
    unit += code >= 0x10000 ? 2 : 1;
    offset += width;
  }
  return unit;
}

int xt_string_is_boundary(xt_string *s, int32_t unit_index) {
  int32_t units = (int32_t)xt_string_utf16_length(s);
  if (unit_index <= 0 || unit_index >= units) return 1;
  if (xt_string_is_ascii(s)) return 1;
  int32_t byte = xt_string_byte_offset(s, unit_index);
  return xt_string_unit_offset(s, byte) == unit_index;
}
