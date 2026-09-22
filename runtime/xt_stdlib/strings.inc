/* -- string methods ------------------------------------------------------- */

static int32_t xt_string_find(xt_string *haystack, xt_string *needle, int32_t from) {
  if (from < 0) from = 0;
  if ((uint32_t)from > haystack->length) return -1;
  if (needle->length == 0) return from;
  for (uint32_t i = (uint32_t)from; i + needle->length <= haystack->length; i++) {
    if (memcmp(haystack->data + i, needle->data, needle->length) == 0) return (int32_t)i;
  }
  return -1;
}

static xt_value xt_string_char_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t index = xt_to_int32(indexValue);
  if (index < 0 || (uint32_t)index >= s->length) return xt_string_from_cstr("");
  return xt_string_new(s->data + index, 1);
}

static xt_value xt_string_char_code_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(NAN);
  int32_t index = xt_to_int32(indexValue);
  if (index < 0 || (uint32_t)index >= s->length) return xt_number(NAN);
  return xt_number((double)(unsigned char)s->data[index]);
}

static xt_value xt_string_index_of(xt_value value, xt_value needleValue, xt_value fromValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_number(-1);
  xt_string *needle = xt_as_string(xt_to_string(needleValue));
  int32_t from = fromValue == XT_UNDEFINED ? 0 : xt_to_int32(fromValue);
  return xt_number((double)xt_string_find(s, needle, from));
}

static xt_value xt_string_slice(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  int32_t end = endValue == XT_UNDEFINED ? len : xt_to_int32(endValue);
  xt_normalize_slice(len, &start, &end);
  return xt_string_new(s->data + start, (size_t)(end - start));
}

static xt_value xt_string_substring(xt_value value, xt_value startValue, xt_value endValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
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
  return xt_string_new(s->data + start, (size_t)(end - start));
}

static xt_value xt_string_substr(xt_value value, xt_value startValue, xt_value lengthValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return xt_string_from_cstr("");
  int32_t len = (int32_t)s->length;
  int32_t start = startValue == XT_UNDEFINED ? 0 : xt_to_int32(startValue);
  if (start < 0) start += len;
  if (start < 0) start = 0;
  if (start > len) start = len;
  int32_t length = lengthValue == XT_UNDEFINED ? len - start : xt_to_int32(lengthValue);
  if (length < 0) length = 0;
  if (start + length > len) length = len - start;
  return xt_string_new(s->data + start, (size_t)length);
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
  if (sep->length == 0) {
    for (uint32_t i = 0; i < s->length; i++) xt_array_push(result, xt_string_new(s->data + i, 1));
  } else {
    uint32_t start = 0;
    uint32_t i = 0;
    while (i + sep->length <= s->length) {
      if (memcmp(s->data + i, sep->data, sep->length) == 0) {
        xt_array_push(result, xt_string_new(s->data + start, i - start));
        i += sep->length;
        start = i;
      } else {
        i++;
      }
    }
    xt_array_push(result, xt_string_new(s->data + start, s->length - start));
  }
  xt_array *array = xt_as_array(result);
  if ((int64_t)array->length > limit) array->length = (uint32_t)limit;
  return result;
}

static xt_value xt_string_case(xt_value value, int upper) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *out = xt_string_alloc(s->length);
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)s->data[i];
    out->data[i] = (char)(upper ? toupper((int)c) : tolower((int)c));
  }
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_trim(xt_value value) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  uint32_t start = 0;
  uint32_t end = s->length;
  while (start < end && isspace((unsigned char)s->data[start])) start++;
  while (end > start && isspace((unsigned char)s->data[end - 1])) end--;
  return xt_string_new(s->data + start, end - start);
}

static xt_value xt_string_replace(xt_value value, xt_value searchValue, xt_value replacementValue) {
  if (xt_is_regexp(searchValue)) return xt_regexp_replace(value, searchValue, replacementValue);
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  int32_t at = xt_string_find(s, search, 0);
  if (at < 0) return value;
  size_t total = s->length - search->length + replacement->length;
  xt_string *out = xt_string_alloc(total);
  memcpy(out->data, s->data, (size_t)at);
  memcpy(out->data + at, replacement->data, replacement->length);
  memcpy(out->data + at + replacement->length, s->data + at + search->length,
         s->length - (size_t)at - search->length);
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
  for (int32_t i = 0; i < count; i++) memcpy(out->data + (size_t)i * s->length, s->data, s->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_starts_with(xt_value value, xt_value searchValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  if (search->length > s->length) return XT_FALSE;
  return xt_bool(memcmp(s->data, search->data, search->length) == 0);
}

static xt_value xt_string_ends_with(xt_value value, xt_value searchValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_FALSE;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  if (search->length > s->length) return XT_FALSE;
  return xt_bool(memcmp(s->data + s->length - search->length, search->data, search->length) == 0);
}

static xt_value xt_string_concat_args(xt_value value, int32_t argc, xt_value *argv) {
  xt_value result = XT_IS_STRING(value) ? value : xt_to_string(value);
  for (int32_t i = 0; i < argc; i++) result = xt_add(result, xt_to_string(argv[i]));
  return result;
}
