/* ------------------------------------------------------------------------- */
/* Constructors                                                              */
/* ------------------------------------------------------------------------- */

xt_value xt_undefined(void) { return XT_UNDEFINED; }
xt_value xt_null(void) { return XT_NULL; }
xt_value xt_bool(int b) { return b ? XT_TRUE : XT_FALSE; }
xt_value xt_number(double d) { return xt_from_double(d); }

xt_string *xt_string_alloc(size_t length) {
  xt_string *s = (xt_string *)xt_alloc(sizeof(xt_string), XT_OBJECT_KIND_STRING);
  s->length = (uint32_t)length;
  /* Keep the terminator in the allocation: many consumers hand `flat` to libc. */
  s->capacity = (uint32_t)length + 1;
  s->flat = (char *)malloc(s->capacity);
  if (!s->flat) {
    fprintf(stderr, "xbintsc: out of memory allocating string\n");
    abort();
  }
  s->flat[length] = '\0';
  s->left = NULL;
  s->right = NULL;
  return s;
}

xt_value xt_string_new(const char *data, size_t len) {
  xt_string *s = xt_string_alloc(len);
  if (len > 0) memcpy(s->flat, data, len);
  return XT_FROM_PTR(XT_TAG_STRING, s);
}

xt_value xt_string_from_cstr(const char *data) { return xt_string_new(data, strlen(data)); }

xt_string *xt_as_string(xt_value v);

char *xt_string_mutable(xt_string *s) { return s ? s->flat : NULL; }

const char *xt_string_data(xt_value value) {
  xt_string *s = xt_as_string(value);
  return s ? xt_string_bytes(s) : NULL;
}

int32_t xt_string_length_value(xt_value value) {
  xt_string *s = xt_as_string(value);
  return s ? (int32_t)s->length : -1;
}
