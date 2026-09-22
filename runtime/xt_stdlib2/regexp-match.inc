/* RegExp support built on POSIX ERE. JavaScript patterns are translated to
 * their POSIX equivalents, capturing groups are tracked so the JS capture
 * indices stay correct even after `(?:` groups are lowered, and the resulting
 * `regmatch_t` offsets are converted back to offsets into the input string. */

static int xt_regex_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* JavaScript regexes use escapes POSIX ERE does not understand (`\r`, `\n`,
 * `\d`, `\w`, `\s`, `(?:...)`, ...). Rewrite the common ones into their POSIX
 * equivalents before handing the pattern to the engine.
 *
 * When `jsGroups` is non-NULL it receives, for each POSIX capturing group
 * (1-based, stored at `jsGroups[i - 1]`), the JavaScript capture index, or -1
 * for a group that was non-capturing in the source (POSIX has no
 * non-capturing groups, so `(?:...)` is emitted as a capturing group).
 * `*groupCount` receives the number of groups seen. `jsGroups` must have room
 * for at least `strlen(pattern) + 1` entries.
 *
 * Returns a malloc'd, NUL-terminated string the caller must free. */
static char *xt_regexp_translate_js(const char *pattern, int *jsGroups, int *groupCount) {
  size_t length = strlen(pattern);
  size_t capacity = length * 16 + 16;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  size_t written = 0;
  int posixGroup = 0;
  int jsCapture = 1;
  int inClass = 0;
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
    } else if (c == '[') {
      inClass = 1;
    } else if (c == ']') {
      inClass = 0;
    } else if (!inClass && c == '(' && i + 2 < length && pattern[i + 1] == '?' && pattern[i + 2] == ':') {
      /* POSIX ERE has no non-capturing groups; a capturing group matches the
       * same text, only the capture bookkeeping differs. */
      literal = '(';
      consume = 2;
    }
    if (!inClass && c == '(') {
      if (jsGroups) jsGroups[posixGroup] = (consume == 2) ? -1 : jsCapture;
      if (consume != 2) jsCapture++;
      posixGroup++;
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
  if (groupCount) *groupCount = posixGroup;
  return out;
}

/* Run `regexpValue` against `input` starting at byte offset `start`. On a
 * match, fills `matches` (up to `nmatch` entries, offsets made absolute) and
 * returns 1; otherwise returns 0 and leaves `matches` untouched. When
 * `jsGroups` is non-NULL it receives the JS capture index for each POSIX
 * group, and `*groupCount` the number of POSIX groups. */
static int xt_regexp_run(xt_value regexpValue, xt_value input, int32_t start,
                         regmatch_t *matches, size_t nmatch,
                         int *jsGroups, int *groupCount) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *pattern = xt_as_string(regexp->source);
  xt_value textValue = xt_to_string(input);
  xt_string *text = xt_as_string(textValue);
  if (!pattern || !text) return 0;
  if (start < 0) start = 0;
  if ((uint32_t)start > text->length) return 0;
  int flags = REG_EXTENDED;
  xt_string *f = xt_as_string(regexp->flags);
  if (f) for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == 'i') flags |= REG_ICASE;
  char *translated = xt_regexp_translate_js(pattern->data, jsGroups, groupCount);
  if (!translated) return 0;
  regex_t compiled;
  if (regcomp(&compiled, translated, flags) != 0) {
    free(translated);
    return 0;
  }
  free(translated);
  int ok = regexec(&compiled, text->data + start, nmatch, matches, 0) == 0;
  regfree(&compiled);
  if (!ok) return 0;
  for (size_t i = 0; i < nmatch; i++) {
    if (matches[i].rm_so >= 0) {
      matches[i].rm_so += start;
      matches[i].rm_eo += start;
    }
  }
  return 1;
}

int xt_regexp_find(xt_value regexpValue, xt_value input, int32_t start, int32_t *matchStart, int32_t *matchEnd) {
  regmatch_t match;
  if (!xt_regexp_run(regexpValue, input, start, &match, 1, NULL, NULL)) return 0;
  if (matchStart) *matchStart = (int32_t)match.rm_so;
  if (matchEnd) *matchEnd = (int32_t)match.rm_eo;
  return 1;
}

static int xt_regexp_is_global(xt_value regexpValue) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *f = xt_as_string(regexp->flags);
  if (!f) return 0;
  for (uint32_t i = 0; i < f->length; i++) if (f->data[i] == 'g') return 1;
  return 0;
}

/* Number of JavaScript capturing groups for the pattern. */
static int xt_regexp_js_group_count(xt_value regexpValue) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *pattern = xt_as_string(regexp->source);
  if (!pattern) return 0;
  int count = (int)pattern->length + 1;
  int *groups = (int *)malloc(sizeof(int) * (size_t)count);
  int groupCount = 0;
  char *translated = xt_regexp_translate_js(pattern->data, groups, &groupCount);
  free(translated);
  int total = 0;
  for (int i = 0; i < groupCount; i++) if (groups[i] > total) total = groups[i];
  free(groups);
  return total;
}

static xt_value xt_regexp_test(xt_value target, xt_value input) {
  return xt_bool(xt_regexp_find(target, input, 0, NULL, NULL));
}

/* JavaScript-style match object: `[0]` is the matched text, `[1..n]` the
 * capture groups, `index` is the match offset and `input` the searched string. */
xt_value xt_regexp_exec(xt_value target, xt_value input) {
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(target);
  xt_string *pattern = xt_as_string(regexp->source);
  size_t cap = (pattern ? pattern->length : 0) + 2;
  regmatch_t *matches = (regmatch_t *)malloc(sizeof(regmatch_t) * cap);
  int *groups = (int *)malloc(sizeof(int) * cap);
  if (!matches || !groups) { free(matches); free(groups); return XT_NULL; }
  xt_value textValue = xt_to_string(input);
  xt_string *text = xt_as_string(textValue);
  int groupCount = 0;
  if (!xt_regexp_run(target, input, 0, matches, cap, groups, &groupCount)) {
    free(matches);
    free(groups);
    return XT_NULL;
  }
  int jsTotal = 0;
  for (int i = 0; i < groupCount; i++) if (groups[i] > jsTotal) jsTotal = groups[i];
  xt_value out = xt_array_new(0, NULL);
  xt_array_push(out, xt_string_new(text->data + matches[0].rm_so, (int64_t)(matches[0].rm_eo - matches[0].rm_so)));
  for (int g = 1; g <= jsTotal; g++) {
    xt_value groupValue = XT_UNDEFINED;
    for (int i = 0; i < groupCount; i++) {
      if (groups[i] != g) continue;
      if (matches[i + 1].rm_so >= 0) {
        groupValue = xt_string_new(text->data + matches[i + 1].rm_so, (int64_t)(matches[i + 1].rm_eo - matches[i + 1].rm_so));
      }
      break;
    }
    xt_array_push(out, groupValue);
  }
  xt_set(out, xt_string_from_cstr("index"), xt_number((double)matches[0].rm_so));
  xt_set(out, xt_string_from_cstr("input"), textValue);
  xt_set(out, xt_string_from_cstr("groups"), XT_UNDEFINED);
  free(matches);
  free(groups);
  return out;
}

/* `String.prototype.match`: a non-global regexp yields the exec result, a
 * global one yields the array of matched substrings (or null). */
xt_value xt_regexp_match(xt_value input, xt_value regexp) {
  if (!xt_regexp_is_global(regexp)) return xt_regexp_exec(regexp, input);
  xt_value textValue = xt_to_string(input);
  xt_string *text = xt_as_string(textValue);
  xt_value results = xt_array_new(0, NULL);
  int32_t cursor = 0;
  int found = 0;
  while (cursor <= (int32_t)text->length) {
    int32_t start = 0;
    int32_t end = 0;
    if (!xt_regexp_find(regexp, input, cursor, &start, &end)) break;
    found = 1;
    xt_array_push(results, xt_string_new(text->data + start, (int64_t)(end - start)));
    cursor = end > start ? end : end + 1;
  }
  return found ? results : XT_NULL;
}

/* -- replacement expansion ------------------------------------------------- */

static void xt_regexp_append(char **out, size_t *length, size_t *capacity, const char *data, size_t count) {
  if (*length + count + 1 > *capacity) {
    *capacity = (*length + count + 1) * 2 + 16;
    *out = (char *)realloc(*out, *capacity);
    if (!*out) abort();
  }
  memcpy(*out + *length, data, count);
  *length += count;
}

static void xt_regexp_append_group(char **out, size_t *length, size_t *capacity,
                                   xt_string *text, regmatch_t *matches, int *groups,
                                   int groupCount, int group) {
  for (int i = 0; i < groupCount; i++) {
    if (groups[i] != group) continue;
    if (matches[i + 1].rm_so >= 0) {
      xt_regexp_append(out, length, capacity, text->data + matches[i + 1].rm_so,
                       (size_t)(matches[i + 1].rm_eo - matches[i + 1].rm_so));
    }
    return;
  }
}

static void xt_regexp_expand_replacement(char **out, size_t *length, size_t *capacity,
                                         xt_string *replacement, xt_string *text,
                                         regmatch_t *matches, int *groups, int groupCount,
                                         int jsTotal) {
  size_t start = (size_t)matches[0].rm_so;
  size_t end = (size_t)matches[0].rm_eo;
  for (size_t i = 0; i < replacement->length; i++) {
    char c = replacement->data[i];
    if (c != '$' || i + 1 >= replacement->length) {
      xt_regexp_append(out, length, capacity, &c, 1);
      continue;
    }
    char next = replacement->data[i + 1];
    if (next == '$') {
      xt_regexp_append(out, length, capacity, "$", 1);
      i++;
    } else if (next == '&') {
      xt_regexp_append(out, length, capacity, text->data + start, end - start);
      i++;
    } else if (next == '`') {
      xt_regexp_append(out, length, capacity, text->data, start);
      i++;
    } else if (next == '\'') {
      xt_regexp_append(out, length, capacity, text->data + end, text->length - end);
      i++;
    } else if (next >= '0' && next <= '9') {
      int value = next - '0';
      int digits = 1;
      if (i + 2 < replacement->length && replacement->data[i + 2] >= '0' && replacement->data[i + 2] <= '9') {
        int two = value * 10 + (replacement->data[i + 2] - '0');
        if (two <= jsTotal) { value = two; digits = 2; }
      }
      if (value >= 1 && value <= jsTotal) {
        xt_regexp_append_group(out, length, capacity, text, matches, groups, groupCount, value);
        i += (size_t)digits;
      } else {
        xt_regexp_append(out, length, capacity, "$", 1);
      }
    } else {
      xt_regexp_append(out, length, capacity, "$", 1);
    }
  }
}

/* `String.prototype.replace`/`replaceAll` with a RegExp search value. */
xt_value xt_regexp_replace(xt_value value, xt_value regexpValue, xt_value replacementValue) {
  xt_string *text = xt_as_string(xt_to_string(value));
  if (!text) return value;
  int global = xt_regexp_is_global(regexpValue);
  int useFunction = XT_IS_FUNCTION(replacementValue);
  xt_string *replacement = NULL;
  if (!useFunction) {
    replacement = xt_as_string(xt_to_string(replacementValue));
    if (!replacement) return value;
  }
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *pattern = xt_as_string(regexp->source);
  size_t cap = (pattern ? pattern->length : 0) + 2;
  regmatch_t *matches = (regmatch_t *)malloc(sizeof(regmatch_t) * cap);
  int *groups = (int *)malloc(sizeof(int) * cap);
  if (!matches || !groups) { free(matches); free(groups); return value; }
  size_t capacity = text->length + 64;
  char *out = (char *)malloc(capacity);
  if (!out) { free(matches); free(groups); return value; }
  size_t length = 0;
  int32_t cursor = 0;
  int replaced = 0;
  int jsTotal = xt_regexp_js_group_count(regexpValue);
  for (;;) {
    int groupCount = 0;
    if (!xt_regexp_run(regexpValue, value, cursor, matches, cap, groups, &groupCount)) break;
    int32_t start = (int32_t)matches[0].rm_so;
    int32_t end = (int32_t)matches[0].rm_eo;
    xt_regexp_append(&out, &length, &capacity, text->data + cursor, (size_t)(start - cursor));
    if (useFunction) {
      xt_value *args = (xt_value *)malloc(sizeof(xt_value) * (size_t)(jsTotal + 3));
      args[0] = xt_string_new(text->data + start, (int64_t)(end - start));
      for (int g = 1; g <= jsTotal; g++) {
        xt_value groupValue = XT_UNDEFINED;
        for (int i = 0; i < groupCount; i++) {
          if (groups[i] != g) continue;
          if (matches[i + 1].rm_so >= 0) {
            groupValue = xt_string_new(text->data + matches[i + 1].rm_so, (int64_t)(matches[i + 1].rm_eo - matches[i + 1].rm_so));
          }
          break;
        }
        args[g] = groupValue;
      }
      args[jsTotal + 1] = xt_number((double)start);
      args[jsTotal + 2] = xt_to_string(value);
      xt_value produced = xt_closure_call(replacementValue, jsTotal + 3, args);
      free(args);
      xt_string *producedText = xt_as_string(xt_to_string(produced));
      if (producedText) xt_regexp_append(&out, &length, &capacity, producedText->data, producedText->length);
    } else {
      xt_regexp_expand_replacement(&out, &length, &capacity, replacement, text, matches, groups, groupCount, jsTotal);
    }
    replaced = 1;
    if (end > start) {
      cursor = end;
    } else {
      /* Zero-width match: copy one byte so the scan always progresses. */
      if (cursor >= (int32_t)text->length) break;
      xt_regexp_append(&out, &length, &capacity, text->data + cursor, 1);
      cursor++;
    }
    if (!global) break;
  }
  if (!replaced) {
    free(out);
    free(matches);
    free(groups);
    return value;
  }
  size_t tail = text->length - (size_t)cursor;
  xt_regexp_append(&out, &length, &capacity, text->data + cursor, tail);
  xt_value result = xt_string_new(out, (int64_t)length);
  free(out);
  free(matches);
  free(groups);
  return result;
}

/* `String.prototype.split` with a RegExp separator. Capture groups are
 * inserted into the result, matching the specification. */
xt_value xt_regexp_split(xt_value value, xt_value regexpValue, xt_value limitValue) {
  xt_value result = xt_array_new(0, NULL);
  xt_string *text = xt_as_string(xt_to_string(value));
  if (!text) return result;
  int64_t limit = limitValue == XT_UNDEFINED ? 0x7fffffff : (int64_t)xt_to_int32(limitValue);
  if (limit <= 0) return result;
  xt_regexp *regexp = (xt_regexp *)XT_GET_PTR(regexpValue);
  xt_string *pattern = xt_as_string(regexp->source);
  size_t cap = (pattern ? pattern->length : 0) + 2;
  regmatch_t *matches = (regmatch_t *)malloc(sizeof(regmatch_t) * cap);
  int *groups = (int *)malloc(sizeof(int) * cap);
  if (!matches || !groups) { free(matches); free(groups); return result; }
  int jsTotal = xt_regexp_js_group_count(regexpValue);
  int32_t len = (int32_t)text->length;
  int32_t p = 0;
  int32_t q = 0;
  while (q < len) {
    int groupCount = 0;
    if (!xt_regexp_run(regexpValue, value, q, matches, cap, groups, &groupCount)) break;
    int32_t start = (int32_t)matches[0].rm_so;
    int32_t end = (int32_t)matches[0].rm_eo;
    if (start != q) { q++; continue; }
    if (end == p) { q++; continue; }
    xt_array_push(result, xt_string_new(text->data + p, (int64_t)(q - p)));
    if ((int64_t)xt_as_array(result)->length >= limit) { free(matches); free(groups); return result; }
    for (int g = 1; g <= jsTotal; g++) {
      xt_value groupValue = XT_UNDEFINED;
      for (int i = 0; i < groupCount; i++) {
        if (groups[i] != g) continue;
        if (matches[i + 1].rm_so >= 0) {
          groupValue = xt_string_new(text->data + matches[i + 1].rm_so, (int64_t)(matches[i + 1].rm_eo - matches[i + 1].rm_so));
        }
        break;
      }
      xt_array_push(result, groupValue);
      if ((int64_t)xt_as_array(result)->length >= limit) { free(matches); free(groups); return result; }
    }
    p = end;
    q = p;
  }
  if ((int64_t)xt_as_array(result)->length < limit) {
    xt_array_push(result, xt_string_new(text->data + p, (int64_t)(len - p)));
  }
  free(matches);
  free(groups);
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
