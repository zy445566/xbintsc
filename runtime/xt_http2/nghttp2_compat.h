/*
 * Forced-include shim for the vendored nghttp2 build.
 *
 * nghttp2's public header uses POSIX `ssize_t`, which the MSVC/UCRT headers
 * clang targets on Windows do not define. The xbintsc runtime header does
 * define it (`rt.h`), but nghttp2's own translation units do not include that
 * header, so the archive build passes this file with `-include`. The guards
 * make it a no-op on POSIX and when `rt.h` was already seen.
 */
#ifndef XT_NGHTTP2_COMPAT_H
#define XT_NGHTTP2_COMPAT_H

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#include <stdint.h>
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif

#endif /* XT_NGHTTP2_COMPAT_H */
