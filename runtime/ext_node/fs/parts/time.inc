/* -- time helpers --------------------------------------------------------- */

/* Node accepts a Date or a number of seconds; Date stores milliseconds. */
static inline double xt_fs_time_seconds(xt_value value) {
  if (XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_DATE) {
    return ((xt_date *)XT_GET_PTR(value))->time / 1000.0;
  }
  return xt_to_number(value);
}
