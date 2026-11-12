/*
 * Include nghttp2 as a static library.
 *
 * The archive is linked directly into the executable, so the public header must
 * not decorate its declarations with `__declspec(dllimport)` on Windows. The
 * vendored library is built with `NGHTTP2_STATICLIB`; consumers must define it
 * before including `<nghttp2/nghttp2.h>` too.
 */
#ifndef XT_NGHTTP2_STATIC_H
#define XT_NGHTTP2_STATIC_H

#ifndef NGHTTP2_STATICLIB
#define NGHTTP2_STATICLIB 1
#endif

#include <nghttp2/nghttp2.h>

#endif /* XT_NGHTTP2_STATIC_H */
