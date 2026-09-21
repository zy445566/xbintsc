xt_value xt_error_ctor(int32_t argc, xt_value *argv) {
  xt_value error = xt_object_new();
  xt_value name = xt_arg_at(argc, argv, 1);
  xt_value message = xt_arg_at(argc, argv, 0);
  xt_object_set(error, xt_string_from_cstr("name"),
                name == XT_UNDEFINED ? xt_string_from_cstr("Error") : name);
  xt_object_set(error, xt_string_from_cstr("message"),
                message == XT_UNDEFINED ? xt_string_from_cstr("") : message);
  return error;
}

/* `super(message)` inside a subclass of the builtin `Error`: initialize the
 * already-allocated instance instead of creating a fresh object. */
xt_value xt_error_init(xt_value thisValue, int32_t argc, xt_value *argv) {
  xt_value name = xt_arg_at(argc, argv, 1);
  xt_value message = xt_arg_at(argc, argv, 0);
  xt_object_set(thisValue, xt_string_from_cstr("name"),
                name == XT_UNDEFINED ? xt_string_from_cstr("Error") : name);
  xt_object_set(thisValue, xt_string_from_cstr("message"),
                message == XT_UNDEFINED ? xt_string_from_cstr("") : message);
  return thisValue;
}
