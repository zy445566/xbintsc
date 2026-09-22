/* -- JSON ----------------------------------------------------------------- */

typedef struct { const char *data; size_t length; size_t pos; } xt_json_cursor;

static void xt_json_skip_ws(xt_json_cursor *c) {
  while (c->pos < c->length) {
    char ch = c->data[c->pos];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') c->pos++;
    else break;
  }
}

static xt_value xt_json_parse_value(xt_json_cursor *c);

static xt_value xt_json_parse_string(xt_json_cursor *c) {
  c->pos++;
  size_t capacity = 32, length = 0;
  char *out = (char *)malloc(capacity);
  if (!out) abort();
  while (c->pos < c->length) {
    char ch = c->data[c->pos++];
    if (ch == '"') break;
    if (ch == '\\' && c->pos < c->length) {
      char esc = c->data[c->pos++];
      if (esc == 'n') ch = '\n';
      else if (esc == 't') ch = '\t';
      else if (esc == 'r') ch = '\r';
      else if (esc == 'b') ch = '\b';
      else if (esc == 'f') ch = '\f';
      else if (esc == 'u') {
        unsigned code = 0;
        for (int i = 0; i < 4 && c->pos < c->length; i++) {
          char h = c->data[c->pos++];
          code <<= 4;
          if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
          else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
        }
        if (code >= 0x80) {
          char tmp[4];
          int bytes = 0;
          if (code < 0x800) { tmp[bytes++] = (char)(0xC0 | (code >> 6)); tmp[bytes++] = (char)(0x80 | (code & 0x3F)); }
          else { tmp[bytes++] = (char)(0xE0 | (code >> 12)); tmp[bytes++] = (char)(0x80 | ((code >> 6) & 0x3F)); tmp[bytes++] = (char)(0x80 | (code & 0x3F)); }
          if (length + (size_t)bytes + 1 > capacity) { capacity = (length + (size_t)bytes + 1) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
          memcpy(out + length, tmp, (size_t)bytes);
          length += (size_t)bytes;
          continue;
        }
        ch = (char)code;
      } else ch = esc;
    }
    if (length + 2 > capacity) { capacity *= 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
    out[length++] = ch;
  }
  xt_value result = xt_string_new(out, length);
  free(out);
  return result;
}

static xt_value xt_json_parse_number(xt_json_cursor *c) {
  size_t start = c->pos;
  if (c->pos < c->length && (c->data[c->pos] == '-' || c->data[c->pos] == '+')) c->pos++;
  while (c->pos < c->length && (isdigit((unsigned char)c->data[c->pos]) || c->data[c->pos] == '.' ||
                                c->data[c->pos] == 'e' || c->data[c->pos] == 'E' ||
                                c->data[c->pos] == '+' || c->data[c->pos] == '-')) c->pos++;
  char buffer[64];
  size_t n = c->pos - start;
  if (n >= sizeof(buffer)) n = sizeof(buffer) - 1;
  memcpy(buffer, c->data + start, n);
  buffer[n] = '\0';
  return xt_number(strtod(buffer, NULL));
}

static xt_value xt_json_parse_value(xt_json_cursor *c) {
  xt_json_skip_ws(c);
  if (c->pos >= c->length) return XT_UNDEFINED;
  char ch = c->data[c->pos];
  if (ch == '"') return xt_json_parse_string(c);
  if (ch == '{') {
    c->pos++;
    xt_value object = xt_object_new();
    xt_json_skip_ws(c);
    if (c->pos < c->length && c->data[c->pos] == '}') { c->pos++; return object; }
    for (;;) {
      xt_json_skip_ws(c);
      if (c->pos >= c->length || c->data[c->pos] != '"') break;
      xt_value key = xt_json_parse_string(c);
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ':') c->pos++;
      xt_value value = xt_json_parse_value(c);
      xt_object_set(object, key, value);
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ',') { c->pos++; continue; }
      if (c->pos < c->length && c->data[c->pos] == '}') c->pos++;
      break;
    }
    return object;
  }
  if (ch == '[') {
    c->pos++;
    xt_value array = xt_array_new(0, NULL);
    xt_json_skip_ws(c);
    if (c->pos < c->length && c->data[c->pos] == ']') { c->pos++; return array; }
    for (;;) {
      xt_array_push(array, xt_json_parse_value(c));
      xt_json_skip_ws(c);
      if (c->pos < c->length && c->data[c->pos] == ',') { c->pos++; continue; }
      if (c->pos < c->length && c->data[c->pos] == ']') c->pos++;
      break;
    }
    return array;
  }
  if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "true", 4) == 0) { c->pos += 4; return XT_TRUE; }
  if (c->pos + 5 <= c->length && memcmp(c->data + c->pos, "false", 5) == 0) { c->pos += 5; return XT_FALSE; }
  if (c->pos + 4 <= c->length && memcmp(c->data + c->pos, "null", 4) == 0) { c->pos += 4; return XT_NULL; }
  return xt_json_parse_number(c);
}

xt_value xt_json_parse(int32_t argc, xt_value *argv) {
  xt_value input = xt_to_string(xt_arg_at(argc, argv, 0));
  xt_string *s = xt_as_string(input);
  if (!s) return XT_UNDEFINED;
  xt_json_cursor cursor = {s->data, s->length, 0};
  return xt_json_parse_value(&cursor);
}

static void xt_json_stringify_value(xt_value value, int indent, int depth, xt_value *out);

static void xt_json_indent(xt_value *out, int indent, int depth) {
  if (indent <= 0) return;
  *out = xt_add(*out, xt_string_from_cstr("\n"));
  for (int i = 0; i < indent * depth; i++) *out = xt_add(*out, xt_string_from_cstr(" "));
}

static void xt_json_stringify_array(xt_value value, int indent, int depth, xt_value *out) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) { *out = xt_add(*out, xt_string_from_cstr("[]")); return; }
  *out = xt_add(*out, xt_string_from_cstr("["));
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    xt_json_indent(out, indent, depth + 1);
    xt_json_stringify_value(array->items[i], indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  *out = xt_add(*out, xt_string_from_cstr("]"));
}

static void xt_json_stringify_object(xt_value value, int indent, int depth, xt_value *out) {
  xt_value keys = xt_object_keys(value);
  xt_array *array = xt_as_array(keys);
  if (!array) { *out = xt_add(*out, xt_string_from_cstr("{}")); return; }
  int any = 0;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value item = xt_object_get(value, array->items[i]);
    if (item != XT_UNDEFINED && !XT_IS_FUNCTION(item)) { any = 1; break; }
  }
  if (!any) { *out = xt_add(*out, xt_string_from_cstr("{}")); return; }
  *out = xt_add(*out, xt_string_from_cstr("{"));
  int written = 0;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value key = array->items[i];
    xt_value item = xt_object_get(value, key);
    /* `undefined` and functions are omitted from objects. */
    if (item == XT_UNDEFINED || XT_IS_FUNCTION(item)) continue;
    if (written > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    written++;
    xt_json_indent(out, indent, depth + 1);
    *out = xt_add(*out, xt_string_from_cstr("\""));
    *out = xt_add(*out, key);
    *out = xt_add(*out, xt_string_from_cstr("\":"));
    if (indent > 0) *out = xt_add(*out, xt_string_from_cstr(" "));
    xt_json_stringify_value(item, indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  *out = xt_add(*out, xt_string_from_cstr("}"));
}

static void xt_json_stringify_value(xt_value value, int indent, int depth, xt_value *out) {
  if (value == XT_UNDEFINED || value == XT_NULL || XT_IS_FUNCTION(value)) {
    *out = xt_add(*out, xt_string_from_cstr("null"));
    return;
  }
  if (XT_IS_BIGINT(value)) {
    xt_throw(xt_string_from_cstr("TypeError: Do not know how to serialize a BigInt"));
    return;
  }
  if (XT_IS_BOOL(value)) { *out = xt_add(*out, value == XT_TRUE ? xt_string_from_cstr("true") : xt_string_from_cstr("false")); return; }
  if (XT_IS_NUMBER(value)) {
    double d = xt_to_double(value);
    if (!isfinite(d)) *out = xt_add(*out, xt_string_from_cstr("null"));
    else *out = xt_add(*out, xt_to_string(value));
    return;
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    *out = xt_add(*out, xt_string_from_cstr("\""));
    for (uint32_t i = 0; i < s->length; i++) {
      char ch = s->data[i];
      if (ch == '"') *out = xt_add(*out, xt_string_from_cstr("\\\""));
      else if (ch == '\\') *out = xt_add(*out, xt_string_from_cstr("\\\\"));
      else if (ch == '\n') *out = xt_add(*out, xt_string_from_cstr("\\n"));
      else if (ch == '\r') *out = xt_add(*out, xt_string_from_cstr("\\r"));
      else if (ch == '\t') *out = xt_add(*out, xt_string_from_cstr("\\t"));
      else *out = xt_add(*out, xt_string_new(&ch, 1));
    }
    *out = xt_add(*out, xt_string_from_cstr("\""));
    return;
  }
  if (XT_IS_ARRAY(value)) { xt_json_stringify_array(value, indent, depth, out); return; }
  if (XT_IS_OBJECT(value)) { xt_json_stringify_object(value, indent, depth, out); return; }
  *out = xt_add(*out, xt_string_from_cstr("null"));
}

xt_value xt_json_stringify(int32_t argc, xt_value *argv) {
  int indent = 0;
  xt_value space = xt_arg_at(argc, argv, 1);
  if (XT_IS_NUMBER(space)) indent = xt_to_int32(space);
  else if (XT_IS_STRING(space)) indent = (int)((xt_string *)XT_GET_PTR(space))->length;
  if (indent < 0) indent = 0;
  if (indent > 10) indent = 10;
  xt_value value = xt_arg_at(argc, argv, 0);
  if (value == XT_UNDEFINED || XT_IS_FUNCTION(value)) return XT_UNDEFINED;
  xt_value out = xt_string_from_cstr("");
  xt_json_stringify_value(value, indent, 0, &out);
  return out;
}
