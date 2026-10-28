/*
 * xbintsc GUI engine — WebGL object records.
 *
 * Handles are integer ids, not pointers: a `deleteBuffer`/`deleteTexture` call
 * must make an old JavaScript handle a no-op rather than a dangling pointer.
 * Every record keeps its CPU-side data (buffer bytes, texture parameters) and
 * the GPU handle it was last uploaded into.
 *
 * Split out of `webgl.h` to keep the context header under the repository's
 * file-length budget; the context owns the tables of these records.
 */
#ifndef XT_GUI_WEBGL_OBJECTS_H
#define XT_GUI_WEBGL_OBJECTS_H

#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "webgl2_constants.h"
#include "webgl_constants.h"

namespace xtgui {

constexpr int kWebGLMaxAttribs = 16;
constexpr int kWebGLMaxTextureUnits = 16;
constexpr int kWebGLMaxDrawBuffers = 4;

struct WebGLBuffer {
  uint32_t id = 0;
  std::vector<uint8_t> data;
  SDL_GPUBuffer *gpu = nullptr;
  uint32_t capacity = 0;
  bool dirty = true;
  /** Half-open byte range of `data` that changed since the last GPU upload. */
  uint32_t dirty_lo = 0;
  uint32_t dirty_hi = 0;
  /** `bufferData` usage hint, reported by `getBufferParameter`. */
  int usage = GL_STATIC_DRAW;
};

struct WebGLTexture {
  uint32_t id = 0;
  int width = 0;
  int height = 0;
  int levels = 1;
  /** The binding target this texture was created for. */
  int target = GL_TEXTURE_2D;
  /** Layers for a cube/2D-array texture, depth for a 3D texture. */
  int layers = 1;
  SDL_GPUTexture *gpu = nullptr;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  /** Depth(-stencil) formats can only be depth attachments. */
  bool depth = false;
  /** Block-compressed textures are sample-only, with one mip level. */
  bool compressed = false;
  /** Usage flags the current `gpu` texture was created with. */
  uint32_t usage = 0;
  bool as_target = false;        // bound as a colour attachment
  bool as_depth_target = false;  // bound as a depth attachment
  bool has_mipmaps = false;
  int min_filter = 0;
  int mag_filter = 0;
  int wrap_s = 0;
  int wrap_t = 0;
  SDL_GPUSampler *sampler = nullptr;
  /** Comparison sampler for a `sampler2DShadow` binding. */
  SDL_GPUSampler *compare_sampler = nullptr;
  bool compare_mode = false;
  int compare_func = GL_LEQUAL;
  bool dirty = false;
  bool defined = false;
};

struct WebGLRenderbuffer {
  uint32_t id = 0;
  int width = 0;
  int height = 0;
  bool stencil = false;
  SDL_GPUTexture *depth = nullptr;
};

struct WebGLFramebuffer {
  uint32_t id = 0;
  uint32_t color_textures[kWebGLMaxDrawBuffers] = {};
  /** Layer rendered to for an array/cube colour attachment. */
  int color_layers[kWebGLMaxDrawBuffers] = {};
  /** `COLOR_ATTACHMENTi` or `NONE` per draw-buffer slot. */
  int draw_buffers[kWebGLMaxDrawBuffers] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE};
  uint32_t depth_texture = 0;
  uint32_t depth_renderbuffer = 0;
  int width = 0;
  int height = 0;
  SDL_GPUTexture *depth = nullptr;  // owned when no renderbuffer/texture supplies one
};

/** Per-attribute array state. */
struct WebGLAttribState {
  bool enabled = false;
  bool integer = false;  // `vertexAttribIPointer` rather than `vertexAttribPointer`
  uint32_t buffer = 0;
  int size = 4;
  int type = GL_FLOAT;
  bool normalized = false;
  int stride = 0;
  int offset = 0;
  int divisor = 0;  // 0 = per vertex, 1 = per instance
  float current[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  /** GPU buffer holding `current[]` while this attribute is disabled. It is
   * created lazily and re-uploaded only when `attrib_value` changes, so a draw
   * never creates a GPU buffer just to supply a constant attribute. */
  SDL_GPUBuffer *constant_gpu = nullptr;
  uint32_t constant_bytes = 0;
  bool constant_dirty = true;
};

/** A WebGL 2 vertex-array object; binding one restores this state. */
struct WebGLVertexArray {
  uint32_t id = 0;
  WebGLAttribState attribs[kWebGLMaxAttribs];
  uint32_t element_buffer = 0;
};

/** A WebGL 2 sampler object, bound to a texture unit with `bindSampler`. */
struct WebGLSampler {
  uint32_t id = 0;
  int min_filter = 0;
  int mag_filter = 0;
  int wrap_s = 0;
  int wrap_t = 0;
  /** `TEXTURE_COMPARE_MODE`/`TEXTURE_COMPARE_FUNC` (WebGL 2 sampler objects
   * carry them too). */
  bool compare_mode = false;
  int compare_func = GL_LEQUAL;
  SDL_GPUSampler *gpu = nullptr;
};

struct WebGLQuery {
  uint32_t id = 0;
};

struct WebGLSync {
  uint32_t id = 0;
};

}  // namespace xtgui

#endif /* XT_GUI_WEBGL_OBJECTS_H */
