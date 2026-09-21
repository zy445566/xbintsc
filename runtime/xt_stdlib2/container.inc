xt_value xt_ext_container_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  xt_object *obj = (xt_object *)XT_GET_PTR(target);
  if (obj->header.kind == XT_OBJECT_KIND_MAP) return xt_map_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_SET) return xt_set_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_DATE) return xt_date_method(target, method, argc, argv, handled);
  if (obj->header.kind == XT_OBJECT_KIND_REGEXP) {
    xt_regexp *regexp = (xt_regexp *)obj;
    *handled = 1;
    if (strcmp(method, "test") == 0) return xt_regexp_test(target, xt_arg_at(argc, argv, 0));
    if (strcmp(method, "exec") == 0) return xt_regexp_exec(target, xt_arg_at(argc, argv, 0));
    if (strcmp(method, "toString") == 0) {
      xt_value out = xt_string_from_cstr("/");
      out = xt_add(out, regexp->source);
      out = xt_add(out, xt_string_from_cstr("/"));
      out = xt_add(out, regexp->flags);
      return out;
    }
    *handled = 0;
    return XT_UNDEFINED;
  }
  *handled = 0;
  return XT_UNDEFINED;
}
