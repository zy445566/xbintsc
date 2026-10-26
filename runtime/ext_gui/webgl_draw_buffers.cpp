/*
 * xbintsc GUI engine — WebGL 2 draw/read buffers and multiple render targets.
 *
 * The framebuffer stores up to four colour attachments and the draw-buffer
 * table that maps pipeline slot N to one of them. `begin_pass` and
 * `create_pipeline` consume the same slots, so a fragment shader's `out`
 * variables land in the attachments the page selected with `drawBuffers`.
 */

#include "webgl.h"

#include "gui_internal.h"

namespace xtgui {

SDL_GPUTexture *WebGLContext::draw_color_texture(int slot) {
  if (slot < 0 || slot >= kWebGLMaxDrawBuffers) return nullptr;
  if (bound_framebuffer_ == 0) {
    ensure_default_target();
    return slot == 0 && default_draw_buffers_[0] == GL_BACK ? color_ : nullptr;
  }
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr) return nullptr;
  const int buffer = framebuffer->draw_buffers[slot];
  const int index = buffer - GL_COLOR_ATTACHMENT0;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) return nullptr;
  if (framebuffer->color_textures[index] == 0) return nullptr;
  WebGLTexture *texture = this->texture(framebuffer->color_textures[index]);
  return texture != nullptr ? ensure_texture_gpu(texture, true) : nullptr;
}

int WebGLContext::draw_color_format(int slot) {
  if (bound_framebuffer_ == 0) return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  if (framebuffer == nullptr) return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  const int index = framebuffer->draw_buffers[slot] - GL_COLOR_ATTACHMENT0;
  if (index < 0 || index >= kWebGLMaxDrawBuffers) {
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  }
  WebGLTexture *texture = this->texture(framebuffer->color_textures[index]);
  return texture != nullptr ? (int)texture->format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

void WebGLContext::draw_buffers(int count, const int *buffers) {
  if (count < 0) count = 0;
  if (count > kWebGLMaxDrawBuffers) count = kWebGLMaxDrawBuffers;
  int *target = default_draw_buffers_;
  if (bound_framebuffer_ != 0) {
    WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
    if (framebuffer == nullptr) return;
    target = framebuffer->draw_buffers;
  }
  for (int i = 0; i < kWebGLMaxDrawBuffers; i++) {
    target[i] = (i < count && buffers != nullptr) ? buffers[i] : GL_NONE;
  }
  end_pass();
}

int WebGLContext::draw_buffer_value(int index) {
  if (index < 0 || index >= kWebGLMaxDrawBuffers) return GL_NONE;
  if (bound_framebuffer_ == 0) return default_draw_buffers_[index];
  WebGLFramebuffer *framebuffer = this->framebuffer(bound_framebuffer_);
  return framebuffer != nullptr ? framebuffer->draw_buffers[index] : GL_NONE;
}

}  // namespace xtgui
