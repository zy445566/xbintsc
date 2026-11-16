/*
 * xbintsc runtime — Web platform globals contributed by `xt_web_*.c`.
 *
 * Declared here rather than in `rt.h`: generated code calls the constructor and
 * static entry points through IR declarations, while these prototypes are only
 * needed by the runtime units that build constructor values from them.
 */

#ifndef XT_WEB_H
#define XT_WEB_H

#include "rt.h"

/* AbortController / AbortSignal (xt_web_abort.c). */
xt_value xt_abort_controller_ctor(int32_t argc, xt_value *argv);
xt_value xt_abort_signal_ctor(int32_t argc, xt_value *argv);
xt_value xt_abort_signal_static(xt_value method, int32_t argc, xt_value *argv);
xt_value xt_web_abort_signal_proto(void);
xt_value xt_web_abort_controller_proto(void);

/* Headers / Response (xt_fetch/webapi.inc). */
xt_value xt_headers_ctor(int32_t argc, xt_value *argv);
xt_value xt_response_ctor(int32_t argc, xt_value *argv);
xt_value xt_web_headers_proto(void);
xt_value xt_web_response_proto(void);

#endif /* XT_WEB_H */
