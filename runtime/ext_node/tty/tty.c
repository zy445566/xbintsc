/*
 * Node.js `tty` module for xbintsc: `isatty(fd)`.
 */

#include "../node_common.h"

#if defined(_WIN32)
#include <io.h>
static int xt_tty_check(int fd) { return _isatty(fd); }
#else
#include <unistd.h>
static int xt_tty_check(int fd) { return isatty(fd); }
#endif

xt_value xt_tty_isatty(int32_t argc, xt_value *argv) {
  int fd = argc > 0 ? (int)xt_to_number(argv[0]) : 0;
  return xt_bool(xt_tty_check(fd) != 0);
}
