/* ------------------------------------------------------------------------- */
/* Strings                                                                   */
/* ------------------------------------------------------------------------- */

xt_string *xt_as_string(xt_value v) {
  if (XT_IS_STRING(v)) return (xt_string *)XT_GET_PTR(v);
  return NULL;
}

int32_t xt_string_length(xt_string *s) { return (int32_t)s->length; }

static xt_value xt_string_concat(xt_string *a, xt_string *b) {
  size_t total = (size_t)a->length + (size_t)b->length;
  xt_string *out = xt_string_alloc(total);
  memcpy(out->data, a->data, a->length);
  memcpy(out->data + a->length, b->data, b->length);
  return XT_FROM_PTR(XT_TAG_STRING, out);
}

int xt_string_equals(xt_string *a, xt_string *b) {
  return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}
