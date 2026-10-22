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
        /* A high surrogate followed by `\u` and a low surrogate denotes one
         * supplementary code point, exactly like a JavaScript string. */
        if (code >= 0xD800 && code <= 0xDBFF && c->pos + 5 < c->length &&
            c->data[c->pos] == '\\' && c->data[c->pos + 1] == 'u') {
          unsigned low = 0;
          int valid = 1;
          for (int i = 0; i < 4; i++) {
            char h = c->data[c->pos + 2 + (size_t)i];
            low <<= 4;
            if (h >= '0' && h <= '9') low |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') low |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') low |= (unsigned)(h - 'A' + 10);
            else valid = 0;
          }
          if (valid && low >= 0xDC00 && low <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            c->pos += 6;
          }
        }
        if (code >= 0x80) {
          char tmp[4];
          size_t bytes = xt_utf8_encode(code, tmp);
          if (length + bytes + 1 > capacity) { capacity = (length + bytes + 1) * 2; out = (char *)realloc(out, capacity); if (!out) abort(); }
          memcpy(out + length, tmp, bytes);
          length += bytes;
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
  xt_json_cursor cursor = {xt_string_bytes(s), s->length, 0};
  return xt_json_parse_value(&cursor);
}

static void xt_json_stringify_value(xt_value value, xt_value indent, int depth, xt_value *out);

/* The indentation unit for the `space` argument: up to 10 spaces for a
 * number, or the first 10 UTF-16 code units of a string (any characters,
 * not just spaces), matching JSON.stringify. An empty string means compact. */
static xt_value xt_json_space_unit(xt_value space) {
  if (XT_IS_NUMBER(space)) {
    int count = xt_to_int32(space);
    if (count < 0) count = 0;
    if (count > 10) count = 10;
    char spaces[10];
    memset(spaces, ' ', (size_t)count);
    return xt_string_new(spaces, (size_t)count);
  }
  if (XT_IS_STRING(space)) {
    xt_string *s = xt_as_string(space);
    const char *bytes = xt_string_bytes(s);
    size_t offset = 0;
    size_t end = 0;
    uint32_t units = 0;
    while (offset < s->length && units < 10) {
      size_t width = 0;
      uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
      units += code >= 0x10000 ? 2 : 1;
      offset += width;
      if (units <= 10) end = offset;
    }
    return xt_string_new(bytes, end);
  }
  return xt_string_from_cstr("");
}

static int xt_json_indented(xt_value unit) {
  return XT_IS_STRING(unit) && xt_as_string(unit)->length > 0;
}

static void xt_json_indent(xt_value *out, xt_value unit, int depth) {
  if (!xt_json_indented(unit)) return;
  *out = xt_add(*out, xt_string_from_cstr("\n"));
  for (int i = 0; i < depth; i++) *out = xt_add(*out, unit);
}

/* Containers on the current serialization path. A repeated pointer is a cycle;
 * serializing it would recurse until the native stack overflowed (a hard crash,
 * not the TypeError the platform throws). */
static xt_value *xt_json_path = NULL;
static uint32_t xt_json_path_count = 0;
static uint32_t xt_json_path_capacity = 0;

static void xt_json_enter(xt_value value) {
  for (uint32_t i = 0; i < xt_json_path_count; i++) {
    if (xt_json_path[i] == value) {
      xt_value message = xt_string_from_cstr("Converting circular structure to JSON");
      xt_throw(xt_type_error_ctor(1, &message));
    }
  }
  if (xt_json_path_count >= xt_json_path_capacity) {
    uint32_t capacity = xt_json_path_capacity == 0 ? 16 : xt_json_path_capacity * 2;
    xt_value *path = (xt_value *)realloc(xt_json_path, sizeof(xt_value) * capacity);
    if (!path) abort();
    xt_json_path = path;
    xt_json_path_capacity = capacity;
  }
  xt_json_path[xt_json_path_count++] = value;
}

static void xt_json_leave(void) {
  if (xt_json_path_count > 0) xt_json_path_count--;
}

static void xt_json_stringify_array(xt_value value, xt_value indent, int depth, xt_value *out) {
  xt_array *array = xt_as_array(value);
  if (!array || array->length == 0) { *out = xt_add(*out, xt_string_from_cstr("[]")); return; }
  *out = xt_add(*out, xt_string_from_cstr("["));
  xt_json_enter(value);
  for (uint32_t i = 0; i < array->length; i++) {
    if (i > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    xt_json_indent(out, indent, depth + 1);
    xt_json_stringify_value(array->items[i], indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  xt_json_leave();
  *out = xt_add(*out, xt_string_from_cstr("]"));
}

static void xt_json_stringify_object(xt_value value, xt_value indent, int depth, xt_value *out) {
  xt_value keys = xt_object_keys(value);
  xt_array *array = xt_as_array(keys);
  if (!array) { *out = xt_add(*out, xt_string_from_cstr("{}")); return; }
  int any = 0;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value item = xt_object_get(value, array->items[i]);
    if (item != XT_UNDEFINED && !XT_IS_FUNCTION(item) && !xt_is_symbol(item)) { any = 1; break; }
  }
  if (!any) { *out = xt_add(*out, xt_string_from_cstr("{}")); return; }
  *out = xt_add(*out, xt_string_from_cstr("{"));
  xt_json_enter(value);
  int written = 0;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value key = array->items[i];
    xt_value item = xt_object_get(value, key);
    /* `undefined`, functions and symbols are omitted from objects. */
    if (item == XT_UNDEFINED || XT_IS_FUNCTION(item) || xt_is_symbol(item)) continue;
    if (written > 0) *out = xt_add(*out, xt_string_from_cstr(","));
    written++;
    xt_json_indent(out, indent, depth + 1);
    *out = xt_add(*out, xt_string_from_cstr("\""));
    *out = xt_add(*out, key);
    *out = xt_add(*out, xt_string_from_cstr("\":"));
    if (xt_json_indented(indent)) *out = xt_add(*out, xt_string_from_cstr(" "));
    xt_json_stringify_value(item, indent, depth + 1, out);
  }
  xt_json_indent(out, indent, depth);
  xt_json_leave();
  *out = xt_add(*out, xt_string_from_cstr("}"));
}

static void xt_json_stringify_value(xt_value value, xt_value indent, int depth, xt_value *out) {
  if (value == XT_UNDEFINED || value == XT_NULL || XT_IS_FUNCTION(value) || xt_is_symbol(value)) {
    *out = xt_add(*out, xt_string_from_cstr("null"));
    return;
  }
  if (XT_IS_BIGINT(value)) {
    xt_throw(xt_error_from_message("TypeError: Do not know how to serialize a BigInt"));
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
    const char *bytes = xt_string_bytes(s);
    *out = xt_add(*out, xt_string_from_cstr("\""));
    size_t offset = 0;
    while (offset < s->length) {
      size_t width = 0;
      uint32_t code = xt_utf8_decode(bytes + offset, s->length - offset, &width);
      const char *escape = NULL;
      char buffer[8];
      if (code == '"') escape = "\\\"";
      else if (code == '\\') escape = "\\\\";
      else if (code == '\b') escape = "\\b";
      else if (code == '\f') escape = "\\f";
      else if (code == '\n') escape = "\\n";
      else if (code == '\r') escape = "\\r";
      else if (code == '\t') escape = "\\t";
      else if (code < 0x20 || (code >= 0xD800 && code <= 0xDFFF)) {
        snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned)code);
        escape = buffer;
      }
      if (escape) *out = xt_add(*out, xt_string_from_cstr(escape));
      else *out = xt_add(*out, xt_string_new(bytes + offset, width));
      offset += width;
    }
    *out = xt_add(*out, xt_string_from_cstr("\""));
    return;
  }
  if (XT_IS_ARRAY(value)) { xt_json_stringify_array(value, indent, depth, out); return; }
  if (XT_IS_OBJECT(value)) {
    /* Map/Set/Date/RegExp/Promise/generators are not property-bag objects;
     * handing them to xt_json_stringify_object reads their fields as a property
     * table and crashed with an access violation. JSON sees no enumerable own
     * properties on the first group and calls Date.prototype.toJSON. */
    xt_object *object = (xt_object *)XT_GET_PTR(value);
    switch (object->header.kind) {
      case XT_OBJECT_KIND_MAP:
      case XT_OBJECT_KIND_SET:
      case XT_OBJECT_KIND_REGEXP:
      case XT_OBJECT_KIND_PROMISE:
      case XT_OBJECT_KIND_GENERATOR:
      case XT_OBJECT_KIND_ITERATOR:
      case XT_OBJECT_KIND_ASYNC:
        *out = xt_add(*out, xt_string_from_cstr("{}"));
        return;
      case XT_OBJECT_KIND_DATE: {
        xt_value time = xt_call_method(value, xt_string_from_cstr("getTime"), 0, NULL);
        if (isnan(xt_to_number(time))) {
          *out = xt_add(*out, xt_string_from_cstr("null"));
          return;
        }
        xt_value text = xt_call_method(value, xt_string_from_cstr("toJSON"), 0, NULL);
        xt_json_stringify_value(text, indent, depth, out);
        return;
      }
      default:
        break;
    }
    xt_json_stringify_object(value, indent, depth, out);
    return;
  }
  *out = xt_add(*out, xt_string_from_cstr("null"));
}

/* -- replacer-aware stringify -------------------------------------------- */
/*
 * The replacer overload needs the holder and key of every value, so it gets a
 * separate walk instead of complicating the fast path. It also calls `toJSON`
 * and filters object keys through an array replacer.
 */
typedef struct {
  xt_value replacer;
  xt_value indent;
  xt_value *path;
  uint32_t path_count;
  uint32_t path_capacity;
} xt_json_replacer_context;

static void xt_json_replacer_enter(xt_json_replacer_context *ctx, xt_value value) {
  for (uint32_t i = 0; i < ctx->path_count; i++) {
    if (ctx->path[i] == value) {
      xt_value message = xt_string_from_cstr("Converting circular structure to JSON");
      xt_throw(xt_type_error_ctor(1, &message));
    }
  }
  if (ctx->path_count >= ctx->path_capacity) {
    uint32_t capacity = ctx->path_capacity == 0 ? 16 : ctx->path_capacity * 2;
    xt_value *path = (xt_value *)realloc(ctx->path, sizeof(xt_value) * capacity);
    if (!path) abort();
    ctx->path = path;
    ctx->path_capacity = capacity;
  }
  ctx->path[ctx->path_count++] = value;
}

static void xt_json_replacer_leave(xt_json_replacer_context *ctx) {
  if (ctx->path_count > 0) ctx->path_count--;
}

static void xt_json_replacer_indent(xt_json_replacer_context *ctx, int depth, xt_value *out) {
  xt_json_indent(out, ctx->indent, depth);
}

static int xt_json_replacer_allows(xt_value replacer, xt_value key) {
  if (!XT_IS_ARRAY(replacer)) return 1;
  xt_array *allowed = xt_as_array(replacer);
  for (uint32_t i = 0; i < allowed->length; i++) {
    xt_value item = allowed->items[i];
    if (!XT_IS_STRING(item) && !XT_IS_NUMBER(item)) continue;
    xt_value text = xt_to_string(item);
    xt_string *a = xt_as_string(text);
    xt_string *b = xt_as_string(key);
    if (a && b && xt_string_equals(a, b)) return 1;
  }
  return 0;
}

static int xt_json_replacer_write(xt_json_replacer_context *ctx, xt_value holder, xt_value key,
                                  int depth, xt_value *out);

static int xt_json_replacer_container(xt_json_replacer_context *ctx, xt_value value, int depth,
                                      xt_value *out) {
  xt_json_replacer_enter(ctx, value);
  if (XT_IS_ARRAY(value)) {
    xt_array *array = xt_as_array(value);
    *out = xt_add(*out, xt_string_from_cstr("["));
    for (uint32_t i = 0; i < array->length; i++) {
      if (i > 0) *out = xt_add(*out, xt_string_from_cstr(","));
      xt_json_replacer_indent(ctx, depth + 1, out);
      char keyBuffer[16];
      snprintf(keyBuffer, sizeof(keyBuffer), "%u", i);
      xt_value temporary = xt_string_from_cstr("");
      int wrote = xt_json_replacer_write(ctx, value, xt_string_from_cstr(keyBuffer), depth + 1,
                                         &temporary);
      *out = xt_add(*out, wrote ? temporary : xt_string_from_cstr("null"));
    }
    xt_json_replacer_indent(ctx, depth, out);
    xt_json_replacer_leave(ctx);
    *out = xt_add(*out, xt_string_from_cstr("]"));
    return 1;
  }

  xt_value keys = xt_object_keys(value);
  xt_array *list = xt_as_array(keys);
  *out = xt_add(*out, xt_string_from_cstr("{"));
  int written = 0;
  if (list) {
    for (uint32_t i = 0; i < list->length; i++) {
      xt_value childKey = list->items[i];
      if (!xt_json_replacer_allows(ctx->replacer, childKey)) continue;
      xt_value temporary = xt_string_from_cstr("");
      int wrote = xt_json_replacer_write(ctx, value, childKey, depth + 1, &temporary);
      if (!wrote) continue;
      if (written > 0) *out = xt_add(*out, xt_string_from_cstr(","));
      written++;
      xt_json_replacer_indent(ctx, depth + 1, out);
      *out = xt_add(*out, xt_string_from_cstr("\""));
      *out = xt_add(*out, childKey);
      *out = xt_add(*out, xt_string_from_cstr("\":"));
      if (xt_json_indented(ctx->indent)) *out = xt_add(*out, xt_string_from_cstr(" "));
      *out = xt_add(*out, temporary);
    }
  }
  xt_json_replacer_indent(ctx, depth, out);
  xt_json_replacer_leave(ctx);
  *out = xt_add(*out, xt_string_from_cstr("}"));
  return 1;
}

static int xt_json_replacer_write(xt_json_replacer_context *ctx, xt_value holder, xt_value key,
                                  int depth, xt_value *out) {
  xt_value value = xt_get(holder, key);
  if (XT_IS_FUNCTION(ctx->replacer)) {
    xt_value args[2] = {key, value};
    value = xt_call_with_this(ctx->replacer, holder, 2, args);
  }
  if (xt_is_date(value)) {
    value = xt_call_method(value, xt_string_from_cstr("toJSON"), 1, &key);
  } else if (XT_IS_ARRAY(value) ||
             (XT_IS_OBJECT(value) &&
              (((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_OBJECT ||
               ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_ERROR))) {
    xt_value toJSON = XT_IS_ARRAY(value) ? xt_get(value, xt_string_from_cstr("toJSON"))
                                         : xt_object_get(value, xt_string_from_cstr("toJSON"));
    if (XT_IS_FUNCTION(toJSON)) {
      xt_value args[1] = {key};
      value = xt_call_with_this(toJSON, value, 1, args);
    }
  }
  if (value == XT_UNDEFINED || XT_IS_FUNCTION(value) || xt_is_symbol(value)) return 0;
  if (XT_IS_BIGINT(value)) {
    xt_value message = xt_string_from_cstr("Do not know how to serialize a BigInt");
    xt_throw(xt_type_error_ctor(1, &message));
  }
  if (XT_IS_ARRAY(value)) return xt_json_replacer_container(ctx, value, depth, out);
  if (XT_IS_OBJECT(value)) {
    int kind = ((xt_object *)XT_GET_PTR(value))->header.kind;
    if (kind == XT_OBJECT_KIND_MAP || kind == XT_OBJECT_KIND_SET ||
        kind == XT_OBJECT_KIND_REGEXP || kind == XT_OBJECT_KIND_PROMISE ||
        kind == XT_OBJECT_KIND_GENERATOR || kind == XT_OBJECT_KIND_ITERATOR ||
        kind == XT_OBJECT_KIND_ASYNC) {
      *out = xt_add(*out, xt_string_from_cstr("{}"));
      return 1;
    }
    return xt_json_replacer_container(ctx, value, depth, out);
  }
  xt_json_stringify_value(value, ctx->indent, depth, out);
  return 1;
}

xt_value xt_json_stringify(int32_t argc, xt_value *argv) {
  xt_value value = xt_arg_at(argc, argv, 0);
  if (value == XT_UNDEFINED || XT_IS_FUNCTION(value) || xt_is_symbol(value)) return XT_UNDEFINED;
  /* Signature: stringify(value, replacer, space). The replacer is argv[1] and
   * the indentation is argv[2]; reading argv[1] as `space` silently ignored
   * `JSON.stringify(value, null, 2)`. */
  xt_value indent = xt_json_space_unit(xt_arg_at(argc, argv, 2));
  xt_value replacer = xt_arg_at(argc, argv, 1);
  if (XT_IS_FUNCTION(replacer) || XT_IS_ARRAY(replacer)) {
    xt_json_replacer_context context = {replacer, indent, NULL, 0, 0};
    xt_value wrapper = xt_object_new();
    xt_object_set(wrapper, xt_string_from_cstr(""), value);
    xt_value out = xt_string_from_cstr("");
    int wrote = xt_json_replacer_write(&context, wrapper, xt_string_from_cstr(""), 0, &out);
    free(context.path);
    return wrote ? out : XT_UNDEFINED;
  }
  /* A caught circular-structure error longjmps past xt_json_leave; start every
   * top-level serialization with a clean path. */
  xt_json_path_count = 0;
  xt_value out = xt_string_from_cstr("");
  xt_json_stringify_value(value, indent, 0, &out);
  return out;
}
