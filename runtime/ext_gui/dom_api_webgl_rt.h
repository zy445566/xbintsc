/*
 * xbintsc GUI engine — the runtime's typed-array/ArrayBuffer accessors.
 *
 * Those helpers are declared in the C-only `rt_internal.h`; the WebGL bindings
 * are C++, so this small header re-declares the handful they use with C
 * linkage.
 */
#ifndef XT_GUI_DOM_API_WEBGL_RT_H
#define XT_GUI_DOM_API_WEBGL_RT_H

#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

int xt_typed_array_kind_of(xt_value value);
xt_value xt_typed_array_read(xt_value array, uint32_t index);
void xt_typed_array_write(xt_value array, uint32_t index, xt_value value);
xt_value xt_typed_array_buffer_of(xt_value array);
uint32_t xt_typed_array_byte_offset_of(xt_value array);
int xt_is_array_buffer(xt_value value);
uint32_t xt_array_buffer_byte_length(xt_value value);
uint8_t xt_array_buffer_get_byte(xt_value buffer, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* XT_GUI_DOM_API_WEBGL_RT_H */
