/* ------------------------------------------------------------------------- */
/* Output and inspection                                                     */
/* ------------------------------------------------------------------------- */

static void xt_print_value(xt_value v, FILE *out) {
  if (XT_IS_STRING(v)) {
    xt_string *s = xt_as_string(v);
    fwrite(s->data, 1, s->length, out);
    return;
  }
  xt_value text = xt_to_string(v);
  xt_string *s = xt_as_string(text);
  fwrite(s->data, 1, s->length, out);
}

void xt_print(xt_value v) { xt_print_value(v, stdout); }
void xt_println(xt_value v) {
  xt_print_value(v, stdout);
  fputc('\n', stdout);
}

/*
 * console.log applies a Node-like inspection format: strings unquoted at the
 * top level, arrays as `[ a, b ]` and objects as `{ key: value }`.
 *
 * Recursion tracks the ancestor chain so self-referential containers print
 * Node's `[Circular *N]` marker instead of recursing forever. `N` is the
 * 1-based position of the repeated container in the current path.
 */
#define XT_INSPECT_MAX_DEPTH 512
static const void *xt_inspect_stack[XT_INSPECT_MAX_DEPTH];
static int xt_inspect_depth = 0;

static int xt_inspect_seen(const void *ptr) {
  for (int i = 0; i < xt_inspect_depth; i++) {
    if (xt_inspect_stack[i] == ptr) return i + 1;
  }
  return 0;
}

static void xt_inspect_push(const void *ptr) {
  if (xt_inspect_depth < XT_INSPECT_MAX_DEPTH) xt_inspect_stack[xt_inspect_depth++] = ptr;
}

static void xt_inspect_pop(void) {
  if (xt_inspect_depth > 0) xt_inspect_depth--;
}

static int xt_inspect_identifier_key(xt_string *key) {
  if (key->length == 0) return 0;
  char c = key->data[0];
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')) return 0;
  for (uint32_t i = 1; i < key->length; i++) {
    c = key->data[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$')) return 0;
  }
  return 1;
}

/* Render a string the way `util.inspect` does inside a container: quoted,
 * preferring whichever quote character needs fewer escapes. */
static void xt_inspect_string(xt_string *s, FILE *out) {
  int hasSingle = 0;
  int hasDouble = 0;
  for (uint32_t i = 0; i < s->length; i++) {
    if (s->data[i] == '\'') hasSingle = 1;
    else if (s->data[i] == '"') hasDouble = 1;
  }
  char quote = (hasSingle && !hasDouble) ? '"' : '\'';
  fputc(quote, out);
  for (uint32_t i = 0; i < s->length; i++) {
    unsigned char c = (unsigned char)s->data[i];
    switch (c) {
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      case '\f': fputs("\\f", out); break;
      case '\b': fputs("\\b", out); break;
      case '\v': fputs("\\v", out); break;
      default:
        if (c == (unsigned char)quote) { fputc('\\', out); fputc(c, out); }
        else if (c < 0x20) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
  }
  fputc(quote, out);
}

static void xt_inspect(xt_value v, FILE *out, int nested) {
  if (xt_is_symbol(v)) {
    xt_string *text = xt_as_string(xt_symbol_to_string(v));
    fwrite(text->data, 1, text->length, out);
    return;
  }
  if (XT_IS_BIGINT(v)) {
    xt_value text = xt_bigint_to_decimal(v);
    xt_string *s = xt_as_string(text);
    fwrite(s->data, 1, s->length, out);
    fputc('n', out);
    return;
  }
  if (nested && XT_IS_STRING(v)) {
    xt_inspect_string(xt_as_string(v), out);
    return;
  }
  if (XT_IS_ARRAY(v)) {
    xt_array *array = (xt_array *)XT_GET_PTR(v);
    int seen = xt_inspect_seen(array);
    if (seen > 0) {
      fprintf(out, "[Circular *%d]", seen);
      return;
    }
    if (xt_inspect_depth >= XT_INSPECT_MAX_DEPTH) {
      fputs("[Array]", out);
      return;
    }
    xt_inspect_push(array);
    fputc('[', out);
    for (uint32_t i = 0; i < array->length; i++) {
      if (i > 0) fputs(", ", out);
      else fputc(' ', out);
      xt_inspect(array->items[i], out, 1);
    }
    if (XT_IS_OBJECT(array->extra)) {
      xt_object *extra = (xt_object *)XT_GET_PTR(array->extra);
      for (uint32_t i = 0; i < extra->count; i++) {
        if (array->length > 0 || i > 0) fputs(", ", out);
        else fputc(' ', out);
        xt_value key = extra->properties[i].key;
        if (xt_is_symbol(key)) {
          xt_string *text = xt_as_string(xt_symbol_to_string(key));
          fwrite(text->data, 1, text->length, out);
        } else {
          xt_string *keyString = xt_as_string(key);
          fwrite(keyString->data, 1, keyString->length, out);
        }
        fputs(": ", out);
        xt_inspect(extra->properties[i].value, out, 1);
      }
    }
    if (array->length > 0 || (XT_IS_OBJECT(array->extra) && ((xt_object *)XT_GET_PTR(array->extra))->count > 0)) fputc(' ', out);
    fputc(']', out);
    xt_inspect_pop();
    return;
  }
  if (XT_IS_OBJECT(v)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(v);
    int seen = xt_inspect_seen(obj);
    if (seen > 0) {
      fprintf(out, "[Circular *%d]", seen);
      return;
    }
    if (xt_inspect_depth >= XT_INSPECT_MAX_DEPTH) {
      fputs("[Object]", out);
      return;
    }
    xt_inspect_push(obj);
    fputc('{', out);
    /* `util.inspect` lists string keys first, then symbol keys, in insertion
     * order; symbol keys are printed without brackets. */
    int written = 0;
    for (int pass = 0; pass < 2; pass++) {
      for (uint32_t i = 0; i < obj->count; i++) {
        xt_value key = obj->properties[i].key;
        int isSymbol = xt_is_symbol(key);
        if ((pass == 0) == isSymbol) continue;
        if (written > 0) fputs(", ", out);
        else fputc(' ', out);
        written++;
        if (isSymbol) {
          xt_string *text = xt_as_string(xt_symbol_to_string(key));
          fwrite(text->data, 1, text->length, out);
        } else {
          xt_string *keyString = xt_as_string(key);
          if (xt_inspect_identifier_key(keyString)) fwrite(keyString->data, 1, keyString->length, out);
          else xt_inspect_string(keyString, out);
        }
        fputs(": ", out);
        xt_inspect(obj->properties[i].value, out, 1);
      }
    }
    if (written > 0) fputc(' ', out);
    fputc('}', out);
    xt_inspect_pop();
    return;
  }
  xt_print_value(v, out);
}

/* Inspect one top-level value; reset the ancestor chain so nested console
 * calls (or a `longjmp` out of a throw) never leave stale state behind. */
static void xt_inspect_top(xt_value v, FILE *out) {
  xt_inspect_depth = 0;
  xt_inspect(v, out, 0);
}

void xt_console_log(int32_t argc, xt_value *argv) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', stdout);
    xt_inspect_top(argv[i], stdout);
  }
  fputc('\n', stdout);
}

static void xt_console_write(int32_t argc, xt_value *argv, FILE *out) {
  for (int32_t i = 0; i < argc; i++) {
    if (i > 0) fputc(' ', out);
    xt_inspect_top(argv[i], out);
  }
  fputc('\n', out);
}
