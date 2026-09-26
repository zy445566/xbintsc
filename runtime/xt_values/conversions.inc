/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */

int xt_truthy(xt_value v) {
  if (XT_IS_NUMBER(v)) {
    double d = xt_to_double(v);
    return d != 0.0 && !isnan(d);
  }
  if (v == XT_UNDEFINED || v == XT_NULL) return 0;
  if (v == XT_FALSE) return 0;
  if (v == XT_TRUE) return 1;
  if (XT_IS_BIGINT(v)) return XT_GET_PTR(v) != NULL && ((xt_bigint *)XT_GET_PTR(v))->sign != 0;
  if (XT_IS_STRING(v)) return xt_as_string(v)->length != 0;
  return 1; /* objects, arrays and functions are truthy */
}

double xt_to_number(xt_value v) {
  if (XT_IS_NUMBER(v)) return xt_to_double(v);
  if (v == XT_TRUE) return 1.0;
  if (v == XT_FALSE) return 0.0;
  if (v == XT_UNDEFINED) return NAN;
  if (v == XT_NULL) return 0.0;
  if (XT_IS_BIGINT(v)) return xt_bigint_to_double_value(v);
  if (XT_IS_STRING(v)) {
    xt_string *s = xt_as_string(v);
    const char *begin = s->data;
    const char *end = s->data + s->length;
    while (begin < end && isspace((unsigned char)*begin)) begin++;
    while (end > begin && isspace((unsigned char)end[-1])) end--;
    size_t length = (size_t)(end - begin);
    if (length == 0) return 0.0;
    char stackbuf[128];
    char *tmp = length < sizeof(stackbuf) ? stackbuf : (char *)malloc(length + 1);
    memcpy(tmp, begin, length);
    tmp[length] = '\0';
    double result;
    int base = 0;
    if (length > 2 && tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X')) base = 16;
    else if (length > 2 && tmp[0] == '0' && (tmp[1] == 'o' || tmp[1] == 'O')) base = 8;
    else if (length > 2 && tmp[0] == '0' && (tmp[1] == 'b' || tmp[1] == 'B')) base = 2;
    if (base != 0) {
      char *stop = NULL;
      unsigned long long parsed = strtoull(tmp + 2, &stop, base);
      result = (*stop == '\0' && stop != tmp + 2) ? (double)parsed : NAN;
    } else {
      char *stop = NULL;
      result = strtod(tmp, &stop);
      if (stop != tmp + length) result = NAN;
    }
    if (tmp != stackbuf) free(tmp);
    return result;
  }
  return NAN;
}

static xt_value xt_number_to_string_value(double d) {
  char buffer[64];
  xt_number_to_js_string(d, buffer, sizeof(buffer));
  return xt_string_from_cstr(buffer);
}

xt_value xt_to_string(xt_value v) {
  if (XT_IS_STRING(v)) return v;
  if (XT_IS_NUMBER(v)) return xt_number_to_string_value(xt_to_double(v));
  if (v == XT_UNDEFINED) return xt_string_from_cstr("undefined");
  if (v == XT_NULL) return xt_string_from_cstr("null");
  if (v == XT_TRUE) return xt_string_from_cstr("true");
  if (v == XT_FALSE) return xt_string_from_cstr("false");
  if (XT_IS_BIGINT(v)) return xt_bigint_to_decimal(v);
  if (XT_IS_ARRAY(v)) {
    xt_array *a = (xt_array *)XT_GET_PTR(v);
    /* Join elements with commas, like Array.prototype.toString. */
    size_t capacity = 32;
    char *out = (char *)malloc(capacity);
    size_t length = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < a->length; i++) {
      xt_value s = xt_to_string(a->items[i]);
      xt_string *str = xt_as_string(s);
      if (length + str->length + 2 > capacity) {
        capacity = (length + str->length + 2) * 2;
        out = (char *)realloc(out, capacity);
      }
      if (i > 0) out[length++] = ',';
      memcpy(out + length, str->data, str->length);
      length += str->length;
      out[length] = '\0';
    }
    xt_value result = xt_string_new(out, length);
    free(out);
    return result;
  }
  if (XT_IS_FUNCTION(v)) return xt_string_from_cstr("function () { [native code] }");
  if (xt_is_symbol(v)) {
    xt_throw(xt_string_from_cstr("TypeError: Cannot convert a Symbol value to a string"));
  }
  if (XT_IS_OBJECT(v) &&
      ((xt_object *)XT_GET_PTR(v))->header.kind == XT_OBJECT_KIND_ERROR) {
    return xt_error_to_string(v);
  }
  return xt_string_from_cstr("[object Object]");
}

xt_value xt_typeof(xt_value v) {
  if (XT_IS_NUMBER(v)) return xt_string_from_cstr("number");
  if (XT_IS_STRING(v)) return xt_string_from_cstr("string");
  if (v == XT_TRUE || v == XT_FALSE) return xt_string_from_cstr("boolean");
  if (XT_IS_BIGINT(v)) return xt_string_from_cstr("bigint");
  if (v == XT_UNDEFINED) return xt_string_from_cstr("undefined");
  if (XT_IS_FUNCTION(v)) return xt_string_from_cstr("function");
  if (v == XT_NULL) return xt_string_from_cstr("object");
  if (xt_is_symbol(v)) return xt_string_from_cstr("symbol");
  return xt_string_from_cstr("object");
}
