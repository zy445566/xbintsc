/* -- string methods ------------------------------------------------------- */

static xt_value xt_string_pad(xt_value value, xt_value lengthValue, xt_value padValue, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  int32_t target = xt_to_int32(lengthValue);
  if (target <= (int32_t)s->length) return value;
  xt_string *pad = xt_as_string(xt_to_string(padValue == XT_UNDEFINED ? xt_string_from_cstr(" ") : padValue));
  if (!pad || pad->length == 0) return value;
  size_t total = (size_t)target;
  xt_string *out = xt_string_alloc(total);
  size_t needed = total - s->length;
  if (start) {
    for (size_t i = 0; i < needed; i++) out->data[i] = pad->data[i % pad->length];
    memcpy(out->data + needed, s->data, s->length);
  } else {
    memcpy(out->data, s->data, s->length);
    for (size_t i = 0; i < needed; i++) out->data[s->length + i] = pad->data[i % pad->length];
  }
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

static xt_value xt_string_trim_side(xt_value value, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  uint32_t begin = 0;
  uint32_t end = s->length;
  if (start) { while (begin < end && isspace((unsigned char)s->data[begin])) begin++; }
  else { while (end > begin && isspace((unsigned char)s->data[end - 1])) end--; }
  return xt_string_new(s->data + begin, end - begin);
}

static xt_value xt_string_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_UNDEFINED;
  int32_t index = indexValue == XT_UNDEFINED ? 0 : xt_to_int32(indexValue);
  if (index < 0) index += (int32_t)s->length;
  if (index < 0 || (uint32_t)index >= s->length) return XT_UNDEFINED;
  return xt_string_new(s->data + index, 1);
}

static xt_value xt_string_replace_all(xt_value value, xt_value searchValue, xt_value replacementValue) {
  if (xt_is_regexp(searchValue)) return xt_regexp_replace(value, searchValue, replacementValue);
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  if (!search || search->length == 0) return value;
  size_t capacity = s->length + replacement->length * 4 + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t length = 0;
  uint32_t i = 0;
  while (i < s->length) {
    if (i + search->length <= s->length && memcmp(s->data + i, search->data, search->length) == 0) {
      if (length + replacement->length + 2 > capacity) { capacity = (length + replacement->length + 2) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      memcpy(out + length, replacement->data, replacement->length);
      length += replacement->length;
      i += search->length;
    } else {
      if (length + 2 > capacity) { capacity *= 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      out[length++] = s->data[i++];
    }
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

xt_value xt_ext_string_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  if (strcmp(method, "at") == 0) return xt_string_at(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "padStart") == 0) return xt_string_pad(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 1);
  if (strcmp(method, "padEnd") == 0) return xt_string_pad(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1), 0);
  if (strcmp(method, "trimStart") == 0) return xt_string_trim_side(target, 1);
  if (strcmp(method, "trimEnd") == 0) return xt_string_trim_side(target, 0);
  if (strcmp(method, "replaceAll") == 0) return xt_string_replace_all(target, xt_arg_at(argc, argv, 0), xt_arg_at(argc, argv, 1));
  if (strcmp(method, "match") == 0) return xt_regexp_match(target, xt_arg_at(argc, argv, 0));
  if (strcmp(method, "split") == 0) {
    xt_value separator = xt_arg_at(argc, argv, 0);
    if (xt_is_regexp(separator)) return xt_regexp_split(target, separator, xt_arg_at(argc, argv, 1));
    *handled = 0;
    return XT_UNDEFINED;
  }
  if (strcmp(method, "search") == 0) {
    xt_value regexp = xt_arg_at(argc, argv, 0);
    if (xt_is_regexp(regexp)) {
      int32_t start = 0;
      if (xt_regexp_find(regexp, target, 0, &start, NULL)) return xt_number((double)start);
      return xt_number(-1);
    }
    *handled = 0;
    return XT_UNDEFINED;
  }
  if (strcmp(method, "replaceAll") == 0) {
    xt_value searchValue = xt_arg_at(argc, argv, 0);
    if (xt_is_regexp(searchValue)) return xt_regexp_replace(target, searchValue, xt_arg_at(argc, argv, 1));
    *handled = 0;
    return XT_UNDEFINED;
  }
  if (strcmp(method, "lastIndexOf") == 0) {
    xt_string *s = xt_as_string(target);
    xt_string *needle = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
    if (!s || !needle) return xt_number(-1);
    int32_t limit = (argc >= 2 && argv[1] != XT_UNDEFINED) ? xt_to_int32(argv[1]) : (int32_t)s->length;
    if (limit > (int32_t)s->length) limit = (int32_t)s->length;
    int32_t nlen = (int32_t)needle->length;
    if (nlen == 0) return xt_number((double)(limit < 0 ? 0 : limit));
    int32_t i = limit;
    if (i > (int32_t)s->length - nlen) i = (int32_t)s->length - nlen;
    for (; i >= 0; i--) {
      if (memcmp(s->data + i, needle->data, (size_t)nlen) == 0) return xt_number((double)i);
    }
    return xt_number(-1);
  }
  if (strcmp(method, "localeCompare") == 0) {
    xt_string *a = xt_as_string(target);
    xt_string *b = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
    int cmp = strcmp(a ? a->data : "", b ? b->data : "");
    return xt_number(cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
  }
  if (strcmp(method, "codePointAt") == 0) {
    xt_string *s = xt_as_string(target);
    if (!s) return XT_UNDEFINED;
    int32_t index = xt_to_int32(xt_arg_at(argc, argv, 0));
    if (index < 0 || (uint32_t)index >= s->length) return XT_UNDEFINED;
    return xt_number((double)(unsigned char)s->data[index]);
  }
  if (strcmp(method, "valueOf") == 0 || strcmp(method, "toString") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}
