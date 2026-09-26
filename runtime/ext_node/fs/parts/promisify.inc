/* -- promise wrapper ------------------------------------------------------ */

/* A promise wrapper over a synchronous builtin: an immediately settled
 * promise that is rejected (rather than throwing) when the builtin raises. */
static inline xt_value xt_fs_promisify(xt_value (*fn)(int32_t, xt_value *), int32_t argc, xt_value *argv) {
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    xt_value result = fn(argc, argv);
    xt_try_leave(frame);
    return xt_promise_resolve(result);
  }
  xt_value exception = xt_try_exception(frame);
  xt_try_leave(frame);
  return xt_promise_reject(exception);
}
