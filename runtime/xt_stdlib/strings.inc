/* -- string methods ------------------------------------------------------- */

/* Byte-level search. `from` is a byte offset; returns a byte offset or -1. */
static int32_t xt_string_find(xt_string *haystack, xt_string *needle, int32_t from) {
  if (from < 0) from = 0;
  if ((uint32_t)from > haystack->length) return -1;
  if (needle->length == 0) return from;
  const char *hay = xt_string_bytes(haystack);
  const char *need = xt_string_bytes(needle);
  for (uint32_t i = (uint32_t)from; i + needle->length <= haystack->length; i++) {
    if (memcmp(hay + i, need, needle->length) == 0) return (int32_t)i;
  }
  return -1;
}

/* JavaScript indexes strings in UTF-16 code units; the byte search above is
 * still correct because every JS index maps to a code-point boundary. */
static xt_value xt_string_char_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t index = xt_to_int32(indexValue);
  if (index < 0 || (uint32_t)index >= xt_string_utf16_length(s)) return xt_string_from_cstr("");
  return xt_string_unit_value(s, index);
}

static xt_value xt_string_char_code_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(NAN);
  int32_t index = xt_to_int32(indexValue);
  int32_t unit = xt_string_code_unit(s, index);
  if (unit < 0) return xt_number(NAN);
  return xt_number((double)unit);
}

static xt_value xt_string_code_point_at_value(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_UNDEFINED;
  int32_t index = xt_to_int32(indexValue);
  int32_t code = xt_string_code_point_at(s, index);
  if (code < 0) return XT_UNDEFINED;
  return xt_number((double)code);
}

static xt_value xt_string_index_of(xt_value value, xt_value needleValue, xt_value fromValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(-1);
  xt_string *needle = xt_as_string(xt_to_string(needleValue));
  int32_t units = (int32_t)xt_string_utf16_length(s);
  int32_t from = fromValue == XT_UNDEFINED ? 0 : xt_to_int32(fromValue);
  if (from < 0) from = 0;
  if (from > units) from = units;
  /* A start position inside a surrogate pair leaves a lone low half; slice the
   * tail so a search for the whole pair correctly fails. */
  if (!xt_string_is_boundary(s, from)) {
    xt_string *tail = xt_as_string(xt_string_slice_units(s, from, units));
    int32_t at = xt_string_find(tail, needle, 0);
    return xt_number(at < 0 ? -1.0 : (double)(from + xt_string_unit_offset(tail, at)));
  }
  int32_t at = xt_string_find(s, needle, xt_string_byte_offset(s, from));
  return xt_number(at < 0 ? -1.0 : (double)xt_string_unit_offset(s, at));
}

static xt_value xt_string_slice(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)xt_string_utf16_length(s);
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  xt_normalize_slice(len, &start, &end);
  return xt_string_slice_units(s, start, end);
}

static xt_value xt_string_substring(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)xt_string_utf16_length(s);
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  if (start < 0) start = 0;
  if (start > len) start = len;
  if (end < 0) end = 0;
  if (end > len) end = len;
  if (start > end) {
    int32_t swap = start;
    start = end;
    end = swap;
  }
  return xt_string_slice_units(s, start, end);
}

static xt_value xt_string_substr(xt_value value, xt_value startValue, xt_value lengthValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)xt_string_utf16_length(s);
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  if (start < 0) start += len;
  if (start < 0) start = 0;
  if (start > len) start = len;
  int32_t length = lengthValue == XT_UNDEFINED ? len - start : xt_to_int32(lengthValue);
  if (length < 0) length = 0;
  if (start + length > len) length = len - start;
  return xt_string_slice_units(s, start, start + length);
}

static xt_value xt_string_split(xt_value value, xt_value separatorValue, xt_value limitValue) {
  xt_value result = xt_array_new(0, NULL);
  xt_string *s = xt_as_string(value);
  if (!s) return result;
  int64_t limit = limitValue == XT_UNDEFINED ? 0x7fffffff : (int64_t)xt_to_int32(limitValue);
  if (limit <= 0) return result;
  if (separatorValue == XT_UNDEFINED) {
    xt_array_push(result, value);
    return result;
  }
  xt_string *sep = xt_as_string(xt_to_string(separatorValue));
  const char *bytes = xt_string_bytes(s);
  const char *sepBytes = xt_string_bytes(sep);
  if (sep->length == 0) {
    /* Splitting on the empty string yields one string per UTF-16 code unit,
     * so an emoji becomes its two surrogate halves. */
    int32_t units = (int32_t)xt_string_utf16_length(s);
    for (int32_t i = 0; i < units; i++) xt_array_push(result, xt_string_unit_value(s, i));
  } else {
    uint32_t start = 0;
    uint32_t i = 0;
    while (i + sep->length <= s->length) {
      if (memcmp(bytes + i, sepBytes, sep->length) == 0) {
        xt_array_push(result, xt_string_new(bytes + start, i - start));
        i += sep->length;
        start = i;
      } else {
        i++;
      }
    }
    xt_array_push(result, xt_string_new(bytes + start, s->length - start));
  }
  xt_array *array = xt_as_array(result);
  if ((int64_t)array->length > limit) array->length = (uint32_t)limit;
  return result;
}

static xt_value xt_string_case(xt_value value, int upper) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *out = xt_string_alloc(s->length);
  const char *bytes = xt_string_bytes(s);
  char *mutable = xt_string_mutable(out);
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)bytes[i];
    mutable[i] = (char)(upper ? toupper((int)c) : tolower((int)c));
  }
  xt_string_finish(out);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_trim(xt_value value) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  const char *bytes = xt_string_bytes(s);
  uint32_t start = 0;
  uint32_t end = s->length;
  while (start < end && isspace((unsigned char)bytes[start])) start++;
  while (end > start && isspace((unsigned char)bytes[end - 1])) end--;
  return xt_string_new(bytes + start, end - start);
}

static xt_value xt_string_replace(xt_value value, xt_value searchValue, xt_value replacementValue) {
  if (xt_is_regexp(searchValue)) return xt_regexp_replace(value, searchValue, replacementValue);
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  int32_t at = xt_string_find(s, search, 0);
  if (at < 0) return value;
  const char *text = xt_string_bytes(s);
  const char *repl = xt_string_bytes(replacement);
  size_t total = s->length - search->length + replacement->length;
  xt_string *out = xt_string_alloc(total);
  char *mutable = xt_string_mutable(out);
  memcpy(mutable, text, (size_t)at);
  memcpy(mutable + at, repl, replacement->length);
  memcpy(mutable + at + replacement->length, text + at + search->length,
         s->length - (size_t)at - search->length);
  xt_string_finish(out);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_repeat(xt_value value, xt_value countValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  int32_t count = xt_to_int32(countValue);
  if (count < 0) {
    xt_throw(xt_string_from_cstr("RangeError: Invalid count value"));
    return XT_UNDEFINED;
  }
  xt_string *out = xt_string_alloc((size_t)s->length * (size_t)count);
  const char *bytes = xt_string_bytes(s);
  char *mutable = xt_string_mutable(out);
  for (int32_t i = 0; i < count; i++) memcpy(mutable + (size_t)i * s->length, bytes, s->length);
  xt_string_finish(out);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_starts_with(xt_value value, xt_value searchValue, xt_value positionValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  const char *sBytes = xt_string_bytes(s);
  const char *searchBytes = xt_string_bytes(search);
  int32_t units = (int32_t)xt_string_utf16_length(s);
  int32_t position = positionValue == XT_UNDEFINED ? 0 : xt_to_int32(positionValue);
  if (position < 0) position = 0;
  if (position > units) position = units;
  if (search->length > s->length) return XT_FALSE;
  if (!xt_string_is_boundary(s, position)) {
    xt_string *tail = xt_as_string(xt_string_slice_units(s, position, units));
    if (search->length > tail->length) return XT_FALSE;
    return xt_bool(memcmp(xt_string_bytes(tail), searchBytes, search->length) == 0);
  }
  int32_t at = xt_string_byte_offset(s, position);
  if ((uint32_t)at + search->length > s->length) return XT_FALSE;
  return xt_bool(memcmp(sBytes + at, searchBytes, search->length) == 0);
}

static xt_value xt_string_ends_with(xt_value value, xt_value searchValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  const char *sBytes = xt_string_bytes(s);
  const char *searchBytes = xt_string_bytes(search);
  int32_t units = (int32_t)xt_string_utf16_length(s);
  int32_t end = endValue == XT_UNDEFINED ? units : xt_to_int32(endValue);
  if (end < 0) end = 0;
  if (end > units) end = units;
  if (search->length > s->length) return XT_FALSE;
  if (!xt_string_is_boundary(s, end)) {
    xt_string *prefix = xt_as_string(xt_string_slice_units(s, 0, end));
    if (search->length > prefix->length) return XT_FALSE;
    return xt_bool(memcmp(xt_string_bytes(prefix) + prefix->length - search->length,
                          searchBytes, search->length) == 0);
  }
  int32_t at = xt_string_byte_offset(s, end);
  if ((uint32_t)at < search->length) return XT_FALSE;
  return xt_bool(memcmp(sBytes + at - search->length, searchBytes, search->length) == 0);
}

static xt_value xt_string_concat_args(xt_value value, int32_t argc, xt_value *argv) {
  xt_value result = XT_IS_STRING(value) ? value : xt_to_string(value);
  for (int32_t i = 0; i < argc; i++) result = xt_add(result, xt_to_string(argv[i]));
  return result;
}
