/*
 * xbintsc GUI engine — helpers shared by the WebGL DOM bindings.
 *
 * Split out of `dom_api_internal.h` to keep that header under the repository's
 * file-length budget; `dom_api_internal.h` includes this file, so every WebGL
 * binding sees the same declarations.
 */
#ifndef XT_GUI_DOM_API_WEBGL_INTERNAL_H
#define XT_GUI_DOM_API_WEBGL_INTERNAL_H

#include <cstdint>
#include <string>
#include <vector>

#include "gui_engine.h"

namespace xtgui {
class WebGLContext;
}

namespace domapi {

/** The engine WebGL context behind a context handle, or null. */
xtgui::WebGLContext *webgl_from_context(xt_value self);
/** Like `webgl_from_context`, but null while the context is lost: every GL
 * entry point except `getError`/`isContextLost` is a no-op in that state. */
xtgui::WebGLContext *webgl_live_context(xt_value self);
/** The numeric context id stored on a context handle (0 when absent). */
uint64_t webgl_context_id(xt_value self);
/** Create the JavaScript handle for a canvas's WebGL context. */
xt_value webgl_context_handle(xt_value element, xtgui::WebGLContext *context);
/** The id stored on an object handle under `key`, or 0. */
uint32_t webgl_object_id(xt_value handle, const char *key);
/** Build an object handle (`{__xt_gl_context, __xt_gl_<key>}`). */
xt_value webgl_make_object(xt_value context, const char *key, uint32_t id,
                           const std::string &kind);
double webgl_number(int32_t argc, xt_value *argv, int32_t index, double fallback);
int webgl_int(int32_t argc, xt_value *argv, int32_t index, int fallback);
std::string webgl_string(int32_t argc, xt_value *argv, int32_t index);
/** Read an array/typed array of numbers. */
std::vector<double> webgl_numbers(xt_value value);
/** Read the raw bytes of a typed array / ArrayBuffer (or a byte array). */
std::vector<uint8_t> webgl_bytes(xt_value value);
/** Store `count` bytes into a typed array / array handle. */
void webgl_write_bytes(xt_value target, const uint8_t *bytes, size_t count);
/** Convert a source pixel array (RGBA/RGB/LUMINANCE/…) to RGBA8. */
void webgl_convert_pixels(const uint8_t *source, int count, int format, bool premultiply,
                          std::vector<uint8_t> *out);
/** Decode a `texImage2D` source (`<img>`, `<canvas>`, `<video>`, `ImageData`,
 * `ImageBitmap`) into top-down RGBA8. */
bool webgl_read_source(xt_value source, int *width, int *height,
                       std::vector<uint8_t> *rgba, bool premultiply);
/** Read the `UNPACK_FLIP_Y_WEBGL` / `UNPACK_PREMULTIPLY_ALPHA_WEBGL` flags set
 * through `pixelStorei`. */
void webgl_unpack_flags(xt_value context, bool *flip_y, bool *premultiply);
/** Extension object for a compressed-texture family (null when unsupported,
 * undefined when the name is not one of those extensions). */
xt_value webgl_compressed_extension(const std::string &name, xtgui::WebGLContext *context);

}  // namespace domapi

#endif /* XT_GUI_DOM_API_WEBGL_INTERNAL_H */
