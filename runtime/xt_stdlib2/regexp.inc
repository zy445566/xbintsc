/* -- RegExp (POSIX ERE subset) -------------------------------------------- */

int xt_is_regexp(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_REGEXP;
}

xt_value xt_regexp_ctor(int32_t argc, xt_value *argv) {
  xt_regexp *regexp = (xt_regexp *)xt_alloc(sizeof(xt_regexp), XT_OBJECT_KIND_REGEXP);
  xt_value source = xt_arg_at(argc, argv, 0);
  xt_value flags = xt_arg_at(argc, argv, 1);
  if (XT_IS_OBJECT(source) && ((xt_object *)XT_GET_PTR(source))->header.kind == XT_OBJECT_KIND_REGEXP) {
    xt_regexp *other = (xt_regexp *)XT_GET_PTR(source);
    regexp->source = other->source;
    regexp->flags = flags == XT_UNDEFINED ? other->flags : xt_to_string(flags);
  } else {
    regexp->source = source == XT_UNDEFINED ? xt_string_from_cstr("(?:)") : xt_to_string(source);
    regexp->flags = flags == XT_UNDEFINED ? xt_string_from_cstr("") : xt_to_string(flags);
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, regexp);
}
