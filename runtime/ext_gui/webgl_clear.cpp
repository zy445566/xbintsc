/*
 * xbintsc GUI engine — scissored clear.
 *
 * SDL_GPU render-pass clears ignore the scissor rectangle, but WebGL's `clear`
 * honours it. A scissored colour clear is a blit from a 1×1 clear-colour
 * texture into the scissor rectangle; depth and stencil are cleared by drawing
 * a fullscreen triangle with colour writes masked off and depth/stencil writes
 * enabled (depth test ALWAYS, stencil REPLACE).
 *
 * The internal program and buffer are ordinary engine objects, created lazily
 * on the first scissored depth/stencil clear and kept for the context lifetime.
 * The caller's state is saved and restored around the draw, so `clear` remains
 * an API call that only changes the attachment contents.
 */

#include "webgl.h"

#include <cstring>

#include "gui_internal.h"

namespace xtgui {
namespace {

const char *kClearVertexShader =
    "attribute vec2 a_pos;\n"
    "uniform float u_z;\n"
    "void main() { gl_Position = vec4(a_pos, u_z, 1.0); }\n";

const char *kClearFragmentShader =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.0); }\n";

}  // namespace

void WebGLContext::scissored_clear(int mask) {
  if (guidev::g_device == nullptr) return;
  const bool depth_bit = (mask & GL_DEPTH_BUFFER_BIT) != 0;
  const bool stencil_bit = (mask & GL_STENCIL_BUFFER_BIT) != 0;
  if (!depth_bit && !stencil_bit) return;

  if (clear_program_ == 0) {
    const uint32_t vs = create_shader(GL_VERTEX_SHADER);
    shader_source(vs, kClearVertexShader);
    const uint32_t fs = create_shader(GL_FRAGMENT_SHADER);
    shader_source(fs, kClearFragmentShader);
    if (compile_shader(vs) && compile_shader(fs)) {
      const uint32_t program = create_program();
      attach_shader(program, vs);
      attach_shader(program, fs);
      bind_attrib_location(program, 0, "a_pos");
      if (link_program(program)) {
        clear_program_ = program;
      } else {
        delete_program(program);
      }
    }
    if (clear_program_ == 0) {
      delete_shader(vs);
      delete_shader(fs);
    }
  }
  if (clear_program_ == 0) {
    /* No shader backend: fall back to clearing the whole attachment rather than
     * leaving the buffer untouched. */
    begin_pass(false, depth_bit, stencil_bit);
    return;
  }
  if (clear_buffer_ == 0) {
    clear_buffer_ = create_buffer();
    bind_buffer(GL_ARRAY_BUFFER, clear_buffer_);
    const float vertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    buffer_data(clear_buffer_, (const uint8_t *)vertices, (uint32_t)sizeof(vertices),
                GL_DYNAMIC_DRAW);
  }
  if (clear_buffer_ == 0) {
    begin_pass(false, depth_bit, stencil_bit);
    return;
  }

  const WebGLDrawState saved_state = state_;
  const uint32_t saved_program = current_program_;
  const uint32_t saved_array = bound_array_;
  int saved_viewport[4];
  for (int i = 0; i < 4; i++) saved_viewport[i] = viewport_[i];
  WebGLAttribState saved_attribs[kWebGLMaxAttribs];
  memcpy(saved_attribs, attribs_, sizeof(attribs_));

  state_.blend = false;
  state_.cull = false;
  for (bool &channel : state_.color_mask) channel = false;
  state_.depth_test = depth_bit;
  state_.depth_write = depth_bit;
  state_.depth_func = GL_ALWAYS;
  state_.stencil = stencil_bit;
  if (stencil_bit) {
    for (WebGLStencilFace *face : {&state_.stencil_front, &state_.stencil_back}) {
      face->func = GL_ALWAYS;
      face->ref = clear_stencil_;
      face->value_mask = 0xFF;
      face->write_mask = 0xFF;
      face->fail = GL_KEEP;
      face->zfail = GL_KEEP;
      face->zpass = GL_REPLACE;
    }
  }
  current_program_ = clear_program_;
  int attrib = attrib_location(clear_program_, "a_pos");
  if (attrib < 0 || attrib >= kWebGLMaxAttribs) attrib = 0;
  attribs_[attrib] = WebGLAttribState();
  attribs_[attrib].enabled = true;
  attribs_[attrib].buffer = clear_buffer_;
  attribs_[attrib].size = 2;
  attribs_[attrib].type = GL_FLOAT;
  attribs_[attrib].stride = 8;
  attribs_[attrib].offset = 0;
  int target_width = 0;
  int target_height = 0;
  framebuffer_size(bound_framebuffer_, &target_width, &target_height);
  viewport_[0] = 0;
  viewport_[1] = 0;
  viewport_[2] = target_width;
  viewport_[3] = target_height;

  begin_pass(false, false, false);
  if (pass_ != nullptr) {
    const int z_location = uniform_location(clear_program_, "u_z");
    if (z_location >= 0) {
      const double depth = (double)clear_depth_;
      uniform_floats(clear_program_, z_location, 1, &depth, 1);
    }
    draw_arrays(GL_TRIANGLES, 0, 3);
  }

  state_ = saved_state;
  current_program_ = saved_program;
  bound_array_ = saved_array;
  memcpy(attribs_, saved_attribs, sizeof(attribs_));
  for (int i = 0; i < 4; i++) viewport_[i] = saved_viewport[i];
}

}  // namespace xtgui
