/* -- JSON ----------------------------------------------------------------- */

typedef struct {
  const char *data;
  size_t length;
  size_t pos;
  int depth;
  int error;
} xt_json_cursor;

#define XT_JSON_MAX_DEPTH 512

static void xt_json_skip_ws(xt_json_cursor *c) {
  while (c->pos < c->length) {
    char ch = c->data[c->pos];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') c->pos++;
    else break;
  }
}

static void xt_json_fail(xt_json_cursor *c) { c->error = 1; }

static xt_value xt_json_parse_value(xt_json_cursor *c);

static int xt_json_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static xt_value xt_json_parse_string(xt_json_cursor *c) {
  c->pos++; /* opening quote */
  size_t capacity = 32, length = 0;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  int closed = 0;
  while (c->pos < c->length) {
    unsigned char ch = (unsigned char)c->data[c->pos++];
    if (ch == '"') {
      closed = 1;
      break;
    }
    /* JSON forbids unescaped control characters. */
    if (ch < 0x20) {
      xt_json_fail(c);
      break;
    }
    if (ch == '\\') {
      if (c->pos >= c->length) {
        xt_json_fail(c);
        break;
      }
      char esc = c->data[c->pos++];
      if (esc == 'n') ch = '\n';
      else if (esc == 't') ch = '\t';
      else if (esc == 'r') ch = '\r';
      else if (esc == 'b') ch = '\b';
      else if (esc == 'f') ch = '\f';
      else if (esc == '"' || esc == '\\' || esc == '/') ch = (unsigned char)esc;
      else if (esc == 'u') {
        unsigned code = 0;
        int valid = 1;
        if (c->pos + 4 > c->length) valid = 0;
        for (int i = 0; valid && i < 4; i++) {
          int digit = xt_json_hex_digit(c->data[c->pos + (size_t)i]);
          if (digit < 0) valid = 0;
          else code = (code << 4) | (unsigned)digit;
        }
        if (!valid) {
          xt_json_fail(c);
          break;
        }
        c->pos += 4;
        /* A high surrogate followed by `\u` and a low surrogate denotes one
         * supplementary code point, exactly like a JavaScript string. */
        if (code >= 0xD800 && code <= 0xDBFF && c->pos + 6 <= c->length &&
            c->data[c->pos] == '\\' && c->data[c->pos + 1] == 'u') {
          unsigned low = 0;
          int low_valid = 1;
          for (int i = 0; i < 4; i++) {
            int digit = xt_json_hex_digit(c->data[c->pos + 2 + (size_t)i]);
            if (digit < 0) low_valid = 0;
            else low = (low << 4) | (unsigned)digit;
          }
          if (low_valid && low >= 0xDC00 && low <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            c->pos += 6;
          }
        }
        if (code >= 0x80) {
          char tmp[4];
          size_t bytes = xt_utf8_encode(code, tmp);
          if (length + bytes + 1 > capacity) {
            capacity = (length + bytes + 1) * 2;
            out = (char *)realloc(out, capacity);
            if (!out) abort();
          }
          memcpy(out + length, tmp, bytes);
          length += bytes;
          continue;
        }
        ch = (unsigned char)code;
      } else {
        xt_json_fail(c);
        break;
      }
    }
    if (length + 2 > capacity) {
      capacity *= 2;
      out = (char *)realloc(out, capacity);
      if (!out) abort();
    }
    out[length++] = (char)ch;
  }
  if (!closed || c->error) {
    xt_json_fail(c);
    free(out);
    return XT_UNDEFINED;
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

static xt_value xt_json_parse_number(xt_json_cursor *c) {
  size_t start = c->pos;
  if (c->pos < c->length && c->data[c->pos] == '-') c->pos++;
  if (c->pos >= c->length || !isdigit((unsigned char)c->data[c->pos])) {
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  if (c->data[c->pos] == '0') {
    c->pos++;
  } else {
    while (c->pos < c->length && isdigit((unsigned char)c->data[c->pos])) c->pos++;
  }
  if (c->pos < c->length && c->data[c->pos] == '.') {
    c->pos++;
    if (c->pos >= c->length || !isdigit((unsigned char)c->data[c->pos])) {
      xt_json_fail(c);
      return XT_UNDEFINED;
    }
    while (c->pos < c->length && isdigit((unsigned char)c->data[c->pos])) c->pos++;
  }
  if (c->pos < c->length && (c->data[c->pos] == 'e' || c->data[c->pos] == 'E')) {
    c->pos++;
    if (c->pos < c->length && (c->data[c->pos] == '+' || c->data[c->pos] == '-')) c->pos++;
    if (c->pos >= c->length || !isdigit((unsigned char)c->data[c->pos])) {
      xt_json_fail(c);
      return XT_UNDEFINED;
    }
    while (c->pos < c->length && isdigit((unsigned char)c->data[c->pos])) c->pos++;
  }
  char buffer[64];
  size_t n = c->pos - start;
  if (n >= sizeof(buffer)) {
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  memcpy(buffer, c->data + start, n);
  buffer[n] = '\0';
  return xt_number(strtod(buffer, NULL));
}

static xt_value xt_json_parse_object(xt_json_cursor *c) {
  c->pos++; /* opening brace */
  xt_value object = xt_object_new();
  xt_json_skip_ws(c);
  if (c->pos < c->length && c->data[c->pos] == '}') {
    c->pos++;
    return object;
  }
  for (;;) {
    xt_json_skip_ws(c);
    if (c->pos >= c->length || c->data[c->pos] != '"') {
      xt_json_fail(c);
      return object;
    }
    xt_value key = xt_json_parse_string(c);
    if (c->error) return object;
    xt_json_skip_ws(c);
    if (c->pos >= c->length || c->data[c->pos] != ':') {
      xt_json_fail(c);
      return object;
    }
    c->pos++;
    xt_value value = xt_json_parse_value(c);
    if (c->error) return object;
    xt_object_set(object, key, value);
    xt_json_skip_ws(c);
    if (c->pos >= c->length) {
      xt_json_fail(c);
      return object;
    }
    if (c->data[c->pos] == ',') {
      c->pos++;
      continue;
    }
    if (c->data[c->pos] == '}') {
      c->pos++;
      return object;
    }
    xt_json_fail(c);
    return object;
  }
}

static xt_value xt_json_parse_array(xt_json_cursor *c) {
  c->pos++; /* opening bracket */
  xt_value array = xt_array_new(0, NULL);
  xt_json_skip_ws(c);
  if (c->pos < c->length && c->data[c->pos] == ']') {
    c->pos++;
    return array;
  }
  for (;;) {
    xt_value value = xt_json_parse_value(c);
    if (c->error) return array;
    xt_array_push(array, value);
    xt_json_skip_ws(c);
    if (c->pos >= c->length) {
      xt_json_fail(c);
      return array;
    }
    if (c->data[c->pos] == ',') {
      c->pos++;
      continue;
    }
    if (c->data[c->pos] == ']') {
      c->pos++;
      return array;
    }
    xt_json_fail(c);
    return array;
  }
}

static xt_value xt_json_parse_value(xt_json_cursor *c) {
  xt_json_skip_ws(c);
  if (c->error) return XT_UNDEFINED;
  if (c->pos >= c->length) {
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  char ch = c->data[c->pos];
  if (ch == '"') return xt_json_parse_string(c);
  if (ch == '{' || ch == '[') {
    if (++c->depth > XT_JSON_MAX_DEPTH) {
      xt_json_fail(c);
      return XT_UNDEFINED;
    }
    xt_value result = ch == '{' ? xt_json_parse_object(c) : xt_json_parse_array(c);
    c->depth--;
    return result;
  }
  if (ch == '-' || (ch >= '0' && ch <= '9')) return xt_json_parse_number(c);
  if (ch == 't') {
    if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "true", 4) == 0) {
      c->pos += 4;
      return XT_TRUE;
    }
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  if (ch == 'f') {
    if (c->pos + 5 <= c->length && memcmp(c->data + c->pos, "false", 5) == 0) {
      c->pos += 5;
      return XT_FALSE;
    }
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  if (ch == 'n') {
    if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "null", 4) == 0) {
      c->pos += 4;
      return XT_NULL;
    }
    xt_json_fail(c);
    return XT_UNDEFINED;
  }
  xt_json_fail(c);
  return XT_UNDEFINED;
}

/* Reviver walk: rebuild each container bottom-up, then pass the value to the
 * user callback with its holder as `this`. Returning undefined deletes an
 * object property / array element, exactly like the specification. */
static xt_value xt_json_revive(xt_value holder, xt_value key, xt_value reviver) {
  xt_value value = xt_get(holder, key);
  if (XT_IS_ARRAY(value)) {
    xt_array *array = xt_as_array(value);
    for (uint32_t i = 0; i < array->length; i++) {
      /* Reviver keys are strings even for array indices. */
      char keyBuffer[16];
      snprintf(keyBuffer, sizeof(keyBuffer), "%u", i);
      xt_value index = xt_string_from_cstr(keyBuffer);
      xt_value updated = xt_json_revive(value, index, reviver);
      if (updated == XT_UNDEFINED) xt_delete(value, index);
      else xt_set(value, index, updated);
    }
  } else if (XT_IS_OBJECT(value)) {
    xt_value keys = xt_object_keys(value);
    xt_array *list = xt_as_array(keys);
    if (list) {
      for (uint32_t i = 0; i < list->length; i++) {
        xt_value childKey = list->items[i];
        xt_value updated = xt_json_revive(value, childKey, reviver);
        if (updated == XT_UNDEFINED) xt_delete(value, childKey);
        else xt_object_set(value, childKey, updated);
      }
    }
  }
  xt_value args[2] = {key, value};
  return xt_call_with_this(reviver, holder, 2, args);
}

xt_value xt_json_parse(int32_t argc, xt_value *argv) {
  xt_value input = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_string *s = xt_as_string(input);
  if (!s) {
    xt_value message = xt_string_from_cstr("Unexpected token in JSON");
    xt_throw(xt_error_ctor_by_name(xt_string_from_cstr("SyntaxError"), 1, &message));
  }
  xt_json_cursor cursor = {xt_string_bytes(s), s->length, 0, 0, 0};
  xt_value parsed = xt_json_parse_value(&cursor);
  if (!cursor.error) {
    xt_json_skip_ws(&cursor);
    if (cursor.pos != cursor.length) cursor.error = 1;
  }
  if (cursor.error) {
    char message[96];
    if (cursor.pos >= cursor.length) {
      snprintf(message, sizeof(message), "Unexpected end of JSON input");
    } else {
      snprintf(message, sizeof(message), "Unexpected token in JSON at position %zu",
               cursor.pos);
    }
    xt_value text = xt_string_from_cstr(message);
    xt_throw(xt_error_ctor_by_name(xt_string_from_cstr("SyntaxError"), 1, &text));
  }
  xt_value reviver = xt_arg_at(argc, argv, 1);
  if (!XT_IS_FUNCTION(reviver)) return parsed;
  xt_value holder = xt_object_new();
  xt_object_set(holder, xt_string_from_cstr(""), parsed);
  return xt_json_revive(holder, xt_string_from_cstr(""), reviver);
}
