/* Compile `regexp` and search `input` starting at byte offset `start`.
 * Returns 1 and fills `matchStart`/`matchEnd` (absolute offsets into `input`)
 * when a match is found, otherwise 0. */

static int xt_regex_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* JavaScript regexes use escapes POSIX ERE does not understand (`\r`, `\n`,
 * `\d`, `\w`, `\s`, `(?:...)`, ...). Rewrite the common ones into their POSIX
 * equivalents before handing the pattern to the engine. Returns a malloc'd,
 * NUL-terminated string the caller must free. */
static char *xt_regexp_translate_js(const char *pattern) {
  size_t length = strlen(pattern);
  size_t capacity = length * 16 + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t written = 0;
  for (size_t i = 0; i < length; i++) {
    char c = pattern[i];
    const char *replacement = NULL;
    char literal = 0;
    int consume = 0;
    if (c == '\\') {
      char next = (i + 1 < length) ? pattern[i + 1] : '\0';
      switch (next) {
        case 'r': literal = '\r'; consume = 1; break;
        case 'n': literal = '\n'; consume = 1; break;
        case 't': literal = '\t'; consume = 1; break;
        case 'f': literal = '\f'; consume = 1; break;
        case 'v': literal = '\v'; consume = 1; break;
        case 'd': replacement = "[0-9]"; consume = 1; break;
        case 'D': replacement = "[^0-9]"; consume = 1; break;
        case 'w': replacement = "[A-Za-z0-9_]"; consume = 1; break;
        case 'W': replacement = "[^A-Za-z0-9_]"; consume = 1; break;
        case 's': replacement = "[ \t\n\r\f\v]"; consume = 1; break;
        case 'S': replacement = "[^ \t\n\r\f\v]"; consume = 1; break;
        case 'x': {
          int hi = (i + 2 < length) ? xt_regex_hex_digit(pattern[i + 2]) : -1;
          int lo = (i + 3 < length) ? xt_regex_hex_digit(pattern[i + 3]) : -1;
          if (hi >= 0 && lo >= 0) { literal = (char)(hi * 16 + lo); consume = 3; }
          break;
        }
        default: break;
      }
    } else if (c == '(' && i + 2 < length && pattern[i + 1] == '?' && pattern[i + 2] == ':') {
      /* POSIX ERE has no non-capturing groups; a capturing group matches the
       * same text, only the capture bookkeeping differs. */
      literal = '(';
      consume = 2;
    }
    if (replacement) {
      size_t addition = strlen(replacement);
      if (written + addition + 1 > capacity) { capacity = (written + addition) * 2 + 16; out = (char *)realloc(out, capacity); if (!out) abort(); }
      memcpy(out + written, replacement, addition);
      written += addition;
    } else if (consume || literal) {
      if (written + 2 > capacity) { capacity = capacity * 2 + 16; out = (char *)realloc(out, capacity); if (!out) abort(); }
      out[written++] = literal;
    } else {
      if (written + 2 > capacity) { capacity = capacity * 2 + 16; out = (char *)realloc(out, capacity); if (!out) abort(); }
      out[written++] = c;
    }
    i += (size_t)consume;
  }
  out[written] = '\0';
  return out;
}

int xt_regexp_find(xt_value regexpValue, xt_value input, int32_t start, int32_t *matchStart, int32_t *matchEnd) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *pattern = xt_as_string(regexp->source);
  xt_string *text = xt_as_string(xt_to_string(input));
  if (!pattern || !text) return 0;
  if (start < 0) start = 0;
  if ((uint32_t)start > text->length) return 0;
  int flags = REG_EXTENDED;
  xt_string *f = xt_as_string(regexp->flags);
  if (f) for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == 'i') flags |= REG_ICASE;
  char *translated = xt_regexp_translate_js(pattern->data);
  if (!translated) return 0;
  regex_t compiled;
  if (regcomp(&compiled, translated, flags) != 0) {
    free(translated);
    return 0;
  }
  free(translated);
  regmatch_t match;
  match.rm_so = 0;
  match.rm_eo = 0;
  int ok = regexec(&compiled, text->data + start, 1, &match, 0) == 0;
  regfree(&compiled);
  if (!ok) return 0;
  if (matchStart) *matchStart = start + (int32_t)match.rm_so;
  if (matchEnd) *matchEnd = start + (int32_t)match.rm_eo;
  return 1;
}

static int xt_regexp_is_global(xt_value regexpValue) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *f = xt_as_string(regexp->flags);
  if (!f) return 0;
  for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == 'g') return 1;
  return 0;
}

static xt_value xt_regexp_test(xt_value target, xt_value input) {
  return xt_bool(xt_regexp_find(target, input, 0, NULL, NULL));
}

/* JavaScript-style match object: `[0]` is the matched text, `index` is the
 * match offset and `input` is the searched string. */
xt_value xt_regexp_exec(xt_value target, xt_value input) {
  int32_t start = 0;
  int32_t end = 0;
  if (!xt_regexp_find(target, input, 0, &start, &end)) return XT_NULL;
  xt_value textValue = xt_to_string(input);
  xt_string *text = xt_as_string(textValue);
  xt_value out = xt_object_new();
  xt_object_set(out, xt_string_from_cstr("0"), xt_string_new(text->data + start, (int64_t)(end - start)));
  xt_object_set(out, xt_string_from_cstr("index"), xt_number((double)start));
  xt_object_set(out, xt_string_from_cstr("input"), textValue);
  xt_object_set(out, xt_string_from_cstr("length"), xt_number(1));
  return out;
}

/* `String.prototype.replace`/`replaceAll` with a RegExp search value. */
xt_value xt_regexp_replace(xt_value value, xt_value regexpValue, xt_value replacementValue) {
  xt_string *text = xt_as_string(xt_to_string(value));
  if (!text) return value;
  xt_string *replacement = xt_as_string(xt_to_string(replacementValue));
  if (!replacement) return value;
  int global = xt_regexp_is_global(regexpValue);
  size_t capacity = text->length + replacement->length + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t length = 0;
  int32_t cursor = 0;
  int replaced = 0;
  for (;;) {
    int32_t start = 0;
    int32_t end = 0;
    if (!xt_regexp_find(regexpValue, value, cursor, &start, &end)) break;
    size_t prefix = (size_t)(start - cursor);
    size_t needed = length + prefix + replacement->length + 2;
    if (needed > capacity) {
      capacity = needed * 2;
      out = (char *)realloc(out, capacity);
      if (!out) abort();
    }
    memcpy(out + length, text->data + cursor, prefix);
    length += prefix;
    memcpy(out + length, replacement->data, replacement->length);
    length += replacement->length;
    replaced = 1;
    if (end > start) {
      cursor = end;
    } else {
      /* Zero-width match: copy one byte so the scan always progresses. */
      if (cursor >= (int32_t)text->length) break;
      out[length++] = text->data[cursor++];
    }
    if (!global) break;
  }
  if (!replaced) {
    free(out);
    return value;
  }
  size_t tail = text->length - (size_t)cursor;
  if (length + tail + 1 > capacity) {
    capacity = length + tail + 1;
    out = (char *)realloc(out, capacity);
    if (!out) abort();
  }
  memcpy(out + length, text->data + cursor, tail);
  length += tail;
  xt_value result = xt_string_new(out, (int64_t)length);
  free(out);
  return result;
}

/* Property access (`re.source`, `re.flags`, `re.global`, ...). */
xt_value xt_regexp_get_property(xt_value target, xt_value key) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(target);
  if (!XT_IS_STRING(key)) return XT_UNDEFINED;
  xt_string *k = xt_as_string(key);
  xt_string *f = xt_as_string(regexp->flags);
  int flag = 0;
  if (k->length == 6 && memcmp(k->data, "source", 6) == 0) return regexp->source;
  if (k->length == 5 && memcmp(k->data, "flags", 5) == 0) return regexp->flags;
  if (k->length == 9 && memcmp(k->data, "lastIndex", 9) == 0) return xt_number(0);
  if (k->length == 6 && memcmp(k->data, "global", 6) == 0) flag = 'g';
  else if (k->length == 10 && memcmp(k->data, "ignoreCase", 10) == 0) flag = 'i';
  else if (k->length == 9 && memcmp(k->data, "multiline", 9) == 0) flag = 'm';
  else if (k->length == 6 && memcmp(k->data, "sticky", 6) == 0) flag = 'y';
  if (flag) {
    if (f) for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == flag) return XT_TRUE;
    return XT_FALSE;
  }
  return XT_UNDEFINED;
}
