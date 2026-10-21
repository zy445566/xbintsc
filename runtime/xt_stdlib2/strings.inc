/* -- string methods ------------------------------------------------------- */

/* `padStart`/`padEnd` measure the target length in UTF-16 code units and may
 * cut the pad string through a surrogate pair. */
static xt_value xt_string_pad(xt_value value, xt_value lengthValue, xt_value padValue, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  int32_t target = xt_to_int32(lengthValue);
  int32_t units = (int32_t)xt_string_utf16_length(s);
  if (target <= units) return value;
  xt_value padSymbol = padValue == XT_UNDEFINED ? xt_string_from_cstr(" ") : padValue;
  xt_string *pad = xt_as_string(xt_to_string(padSymbol));
  if (!pad || pad->length == 0) return value;
  int32_t padUnits = (int32_t)xt_string_utf16_length(pad);
  if (padUnits <= 0) return value;
  uint32_t needed = (uint32_t)(target - units);
  xt_value padding = xt_string_from_cstr("");
  uint32_t filled = 0;
  while (filled < needed) {
    uint32_t remaining = needed - filled;
    int32_t chunk = remaining < (uint32_t)padUnits ? (int32_t)remaining : padUnits;
    padding = xt_add(padding, xt_string_slice_units(pad, 0, chunk));
    filled += (uint32_t)chunk;
  }
  return start ? xt_add(padding, value) : xt_add(value, padding);
}

static xt_value xt_string_trim_side(xt_value value, int start) {
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  const char *bytes = xt_string_bytes(s);
  uint32_t begin = 0;
  uint32_t end = s->length;
  if (start) { while (begin < end && isspace((unsigned char)bytes[begin])) begin++; }
  else { while (end > begin && isspace((unsigned char)bytes[end - 1])) end--; }
  return xt_string_new(bytes + begin, end - begin);
}

static xt_value xt_string_at(xt_value value, xt_value indexValue) {
  xt_string *s = xt_as_string(value);
  if (!s) return XT_UNDEFINED;
  int32_t units = (int32_t)xt_string_utf16_length(s);
  int32_t index = indexValue == XT_UNDEFINED ? 0 : xt_to_int32(indexValue);
  if (index < 0) index += units;
  if (index < 0 || index >= units) return XT_UNDEFINED;
  return xt_string_unit_value(s, index);
}

static xt_value xt_string_replace_all(xt_value value, xt_value searchValue, xt_value replacementValue) {
  if (xt_is_regexp(searchValue)) return xt_regexp_replace(value, searchValue, replacementValue);
  xt_string *s = xt_as_string(value);
  if (!s) return value;
  xt_string *search = xt_as_string(xt_to_string(searchValue));
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  if (!search || search->length == 0) {
    /* An empty search inserts the replacement before every UTF-16 code unit
     * and once at the end. */
    int32_t units = (int32_t)xt_string_utf16_length(s);
    xt_value replacementText = xt_to_string(replacementValue);
    xt_value result = replacementText;
    for (int32_t i = 0; i < units; i++) {
      result = xt_add(result, xt_string_unit_value(s, i));
      result = xt_add(result, replacementText);
    }
    return result;
  }
  const char *text = xt_string_bytes(s);
  const char *needle = xt_string_bytes(search);
  const char *repl = xt_string_bytes(replacement);
  size_t capacity = s->length + replacement->length * 4 + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t length = 0;
  uint32_t i = 0;
  while (i < s->length) {
    if (i + search->length <= s->length && memcmp(text + i, needle, search->length) == 0) {
      if (length + replacement->length + 2 > capacity) { capacity = (length + replacement->length + 2) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      memcpy(out + length, repl, replacement->length);
      length += replacement->length;
      i += search->length;
    } else {
      if (length + 2 > capacity) { capacity *= 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
      out[length++] = text[i++];
    }
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

/* `limit` is a UTF-16 index; the last possible match starts at or before it. */
static xt_value xt_string_last_index_of(xt_string *s, xt_string *needle, int32_t limit) {
  if (!s || !needle) return xt_number(-1);
  const char *text = xt_string_bytes(s);
  const char *need = xt_string_bytes(needle);
  int32_t units = (int32_t)xt_string_utf16_length(s);
  if (limit < 0) limit = 0;
  if (limit > units) limit = units;
  if (needle->length == 0) return xt_number((double)limit);
  /* The low half of a surrogate pair is a match candidate of its own, but a
   * byte search from the pair start cannot see it. */
  if (!xt_string_is_boundary(s, limit)) {
    xt_string *tail = xt_as_string(xt_string_slice_units(s, limit, units));
    if (needle->length <= tail->length &&
        memcmp(xt_string_bytes(tail), need, needle->length) == 0)
      return xt_number((double)limit);
  }
  if (needle->length > s->length) return xt_number(-1);
  int32_t i = xt_string_byte_offset(s, limit);
  if (i > (int32_t)s->length - (int32_t)needle->length) i = (int32_t)s->length - (int32_t)needle->length;
  for (; i >= 0; i--) {
    if (((unsigned char)text[i] & 0xC0) == 0x80) continue; /* not a code-point boundary */
    if (memcmp(text + i, need, needle->length) == 0) return xt_number((double)xt_string_unit_offset(s, i));
  }
  return xt_number(-1);
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
      if (xt_regexp_find(regexp, target, 0, &start, NULL)) {
        xt_string *text = xt_as_string(xt_to_string(target));
        return xt_number((double)xt_string_unit_offset(text, start));
      }
      return xt_number(-1);
    }
    *handled = 0;
    return XT_UNDEFINED;
  }
  if (strcmp(method, "lastIndexOf") == 0) {
    xt_string *s = xt_as_string(target);
    xt_string *needle = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
    if (!s || !needle) return xt_number(-1);
    int32_t limit = (argc >= 2 && argv[1] != XT_UNDEFINED) ? xt_to_int32(argv[1])
                                                          : (int32_t)xt_string_utf16_length(s);
    return xt_string_last_index_of(s, needle, limit);
  }
  if (strcmp(method, "localeCompare") == 0) {
    xt_string *a = xt_as_string(target);
    xt_string *b = xt_as_string(xt_to_string(xt_arg_at(argc, argv, 0)));
    int cmp = strcmp(a ? xt_string_bytes(a) : "", b ? xt_string_bytes(b) : "");
    return xt_number(cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
  }
  if (strcmp(method, "codePointAt") == 0) {
    xt_string *s = xt_as_string(target);
    if (!s) return XT_UNDEFINED;
    int32_t index = xt_to_int32(xt_arg_at(argc, argv, 0));
    int32_t code = xt_string_code_point_at(s, index);
    if (code < 0) return XT_UNDEFINED;
    return xt_number((double)code);
  }
  if (strcmp(method, "valueOf") == 0 || strcmp(method, "toString") == 0) return target;
  *handled = 0;
  return XT_UNDEFINED;
}
