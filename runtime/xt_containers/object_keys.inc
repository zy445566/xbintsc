xt_value xt_object_keys(xt_value value) {
  xt_value result = xt_array_new(0, NULL);
  /* A typed array's enumerable own keys are its indices; `length` and the
   * buffer metadata are observable but not enumerable. */
  int typed = xt_typed_array_kind_of(value);
  if (typed >= 0) {
    uint32_t length = (uint32_t)xt_to_number(xt_object_get_cstr(value, "length"));
    for (uint32_t i = 0; i < length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    return result;
  }
  if (XT_IS_OBJECT(value)) {
    xt_object *obj = (xt_object *)XT_GET_PTR(value);
    /* Map/Set/Date/RegExp/Promise/Symbol use their own layout and have no
     * property bag; reading `obj->count` there walked off the end of the
     * allocation (`Object.getOwnPropertyNames(new Map())` crashed). */
    if (!xt_kind_is_object(obj->header.kind)) {
      return result;
    }
    for (uint32_t i = 0; i < obj->count; i++) {
      if (XT_IS_STRING(obj->properties[i].key)) xt_array_push(result, obj->properties[i].key);
    }
    /* Dynamic keys of a host view (`element.dataset`, ...), appended without
     * repeating a real property that was just pushed. */
    xt_object_append_dynamic_keys(obj, value, result);
    return result;
  }
  if (XT_IS_ARRAY(value)) {
    xt_array *array = (xt_array *)XT_GET_PTR(value);
    for (uint32_t i = 0; i < array->length; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    if (XT_IS_OBJECT(array->extra)) {
      xt_object *extra = (xt_object *)XT_GET_PTR(array->extra);
      for (uint32_t i = 0; i < extra->count; i++) {
        if (XT_IS_STRING(extra->properties[i].key)) xt_array_push(result, extra->properties[i].key);
      }
    }
    return result;
  }
  if (XT_IS_STRING(value)) {
    xt_string *s = xt_as_string(value);
    uint32_t units = xt_string_utf16_length(s);
    for (uint32_t i = 0; i < units; i++) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%u", i);
      xt_array_push(result, xt_string_from_cstr(buffer));
    }
    return result;
  }
  return result;
}

