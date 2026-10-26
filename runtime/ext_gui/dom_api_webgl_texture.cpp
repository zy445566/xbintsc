/*
 * xbintsc GUI engine — WebGL texture, framebuffer and readback bindings.
 *
 * `texImage2D` accepts every source a WebGL 1 page uses: a raw pixel array, an
 * `<img>`/`<canvas>` element and an `ImageData`-shaped object. Whatever the
 * source, it is converted to RGBA8 (SDL_GPU's sampler format) on the CPU and
 * uploaded to the GPU; `UNPACK_FLIP_Y_WEBGL` and
 * `UNPACK_PREMULTIPLY_ALPHA_WEBGL` are honoured here.
 */

#include "dom_api_internal.h"

#include <cstring>
#include <string>
#include <vector>

#include "canvas.h"
#include "dom_api_webgl_rt.h"
#include "image.h"
#include "image_bitmap.h"
#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

uint32_t texture_argument(xt_value value) { return webgl_object_id(value, "__xt_gl_texture"); }

int bytes_per_pixel(int format) {
  if (format == GL_RGBA) return 4;
  if (format == GL_RGB) return 3;
  if (format == GL_LUMINANCE_ALPHA) return 2;
  return 1;
}

void flip_rows_bpp(std::vector<uint8_t> *pixels, int width, int height, int bpp) {
  const size_t stride = (size_t)width * bpp;
  std::vector<uint8_t> row(stride);
  for (int y = 0; y < height / 2; y++) {
    uint8_t *top = pixels->data() + (size_t)y * stride;
    uint8_t *bottom = pixels->data() + (size_t)(height - 1 - y) * stride;
    memcpy(row.data(), top, stride);
    memcpy(top, bottom, stride);
    memcpy(bottom, row.data(), stride);
  }
}

float half_to_float(uint16_t half) {
  const uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
  uint32_t exponent = (half >> 10) & 0x1Fu;
  uint32_t mantissa = half & 0x3FFu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa != 0) {
      exponent = 127 - 15 + 1;
      while ((mantissa & 0x400u) == 0) {
        mantissa <<= 1;
        exponent--;
      }
      mantissa &= 0x3FFu;
      bits = sign | (exponent << 23) | (mantissa << 13);
    } else {
      bits = sign;
    }
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
  }
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

uint16_t float_to_half(float value) {
  uint32_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  const uint16_t sign = (uint16_t)((bits >> 16) & 0x8000u);
  const int32_t exponent = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
  uint32_t mantissa = bits & 0x7FFFFFu;
  if (((bits >> 23) & 0xFFu) == 255) {
    return (uint16_t)(sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0));
  }
  if (exponent <= 0) {
    if (exponent < -10) return sign;
    mantissa |= 0x800000u;
    const int shift = 14 - exponent;
    return (uint16_t)(sign | (mantissa >> shift));
  }
  if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
  return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

bool is_depth_internal(int internalformat) {
  return internalformat == GL_DEPTH_COMPONENT16 || internalformat == GL_DEPTH_COMPONENT24 ||
         internalformat == GL_DEPTH_COMPONENT32F || internalformat == GL_DEPTH_STENCIL ||
         internalformat == GL_DEPTH24_STENCIL8;
}

bool is_float_internal(int internalformat) {
  return internalformat == GL_RGBA32F || internalformat == GL_RGBA16F ||
         internalformat == GL_RGB32F || internalformat == GL_RGB16F ||
         internalformat == GL_RG32F || internalformat == GL_RG16F ||
         internalformat == GL_R32F || internalformat == GL_R16F;
}

int source_components(int format) {
  if (format == GL_RGBA) return 4;
  if (format == GL_RGB) return 3;
  if (format == GL_RG || format == GL_LUMINANCE_ALPHA) return 2;
  return 1;
}

size_t component_bytes(int type) {
  if (type == GL_FLOAT) return 4;
  if (type == GL_HALF_FLOAT) return 2;
  return 1;
}

double read_component(const uint8_t *source, size_t index, int type) {
  if (type == GL_FLOAT) {
    float value = 0.0f;
    memcpy(&value, source + index, sizeof(value));
    return value;
  }
  if (type == GL_HALF_FLOAT) {
    uint16_t half = 0;
    memcpy(&half, source + index, sizeof(half));
    return half_to_float(half);
  }
  return (double)source[index] / 255.0;
}

/** Expand float sources into the RGBA layout the SDL float formats expect. */
std::vector<uint8_t> convert_float_pixels(const uint8_t *source, int count, int format, int type,
                                          bool half) {
  const int components = source_components(format);
  const size_t in_bytes = component_bytes(type);
  std::vector<uint8_t> out((size_t)count * 4 * (half ? 2 : 4), 0);
  for (int i = 0; i < count; i++) {
    float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (source != nullptr) {
      double values[4] = {0.0, 0.0, 0.0, 1.0};
      for (int c = 0; c < components; c++) {
        values[c] = read_component(source, ((size_t)i * components + c) * in_bytes, type);
      }
      switch (format) {
        case GL_RGBA:
          rgba[0] = (float)values[0];
          rgba[1] = (float)values[1];
          rgba[2] = (float)values[2];
          rgba[3] = (float)values[3];
          break;
        case GL_RGB:
          rgba[0] = (float)values[0];
          rgba[1] = (float)values[1];
          rgba[2] = (float)values[2];
          break;
        case GL_RG:
          rgba[0] = (float)values[0];
          rgba[1] = (float)values[1];
          break;
        case GL_ALPHA:
          rgba[3] = (float)values[0];
          break;
        case GL_LUMINANCE_ALPHA:
          rgba[0] = rgba[1] = rgba[2] = (float)values[0];
          rgba[3] = (float)values[1];
          break;
        default:
          rgba[0] = rgba[1] = rgba[2] = (float)values[0];
          break;
      }
    }
    if (half) {
      uint16_t *dst = reinterpret_cast<uint16_t *>(out.data()) + (size_t)i * 4;
      for (int c = 0; c < 4; c++) dst[c] = float_to_half(rgba[c]);
    } else {
      float *dst = reinterpret_cast<float *>(out.data()) + (size_t)i * 4;
      for (int c = 0; c < 4; c++) dst[c] = rgba[c];
    }
  }
  return out;
}

/** Convert depth source values to the D32_FLOAT texels SDL expects. */
std::vector<uint8_t> convert_depth_pixels(const uint8_t *source, int count, int type,
                                          bool stencil) {
  std::vector<uint8_t> out((size_t)count * 4, 0);
  if (stencil) {
    /* DEPTH_STENCIL sources use the GL_UNSIGNED_INT_24_8 packing; keep the raw
     * texel, the D32_FLOAT_S8_UINT format consumes the same layout. */
    if (source != nullptr) memcpy(out.data(), source, (size_t)count * 4);
    return out;
  }
  float *dst = reinterpret_cast<float *>(out.data());
  for (int i = 0; i < count; i++) {
    double value = 0.0;
    if (source != nullptr) {
      if (type == GL_UNSIGNED_SHORT) {
        uint16_t bits = 0;
        memcpy(&bits, source + (size_t)i * 2, sizeof(bits));
        value = (double)bits / 65535.0;
      } else if (type == GL_UNSIGNED_INT) {
        uint32_t bits = 0;
        memcpy(&bits, source + (size_t)i * 4, sizeof(bits));
        value = (double)bits / 4294967295.0;
      } else if (type == GL_FLOAT) {
        float bits = 0.0f;
        memcpy(&bits, source + (size_t)i * 4, sizeof(bits));
        value = bits;
      }
    }
    dst[i] = (float)value;
  }
  return out;
}

SDL_GPUTextureFormat storage_format_for(int internalformat) {
  return xtgui::webgl_storage_format(internalformat);
}


}  // namespace

/* -- texture objects ----------------------------------------------------- */

METHOD(webgl_create_texture) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return webgl_make_object(self, "__xt_gl_texture", context->create_texture(), "texture");
}

METHOD(webgl_delete_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->delete_texture(texture_argument(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

METHOD(webgl_bind_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    context->bind_texture(webgl_int(argc, argv, 0, GL_TEXTURE_2D),
                          texture_argument(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_active_texture) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->active_texture(webgl_int(argc, argv, 0, GL_TEXTURE0));
  return XT_UNDEFINED;
}

METHOD(webgl_tex_parameteri) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const uint32_t texture = context->bound_texture(0);
    context->tex_parameter(texture, webgl_int(argc, argv, 1, 0), webgl_int(argc, argv, 2, 0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_tex_parameterf) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) {
    const uint32_t texture = context->bound_texture(0);
    context->tex_parameter(texture, webgl_int(argc, argv, 1, 0),
                           (int)webgl_number(argc, argv, 2, 0.0));
  }
  return XT_UNDEFINED;
}

METHOD(webgl_get_tex_parameter) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_NULL;
  return xt_number(context->tex_parameter_value(context->bound_texture(0),
                                                webgl_int(argc, argv, 1, 0)));
}

METHOD(webgl_tex_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const int target = webgl_int(argc, argv, 0, GL_TEXTURE_2D);
  if (target != GL_TEXTURE_2D) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) {
    context->set_error(GL_INVALID_OPERATION);
    return XT_UNDEFINED;
  }
  const int level = webgl_int(argc, argv, 1, 0);
  if (level != 0) return XT_UNDEFINED;  // mip levels beyond 0 are not uploaded
  bool flip_y = false;
  bool premultiply = false;
  webgl_unpack_flags(self, &flip_y, &premultiply);

  int width = 0;
  int height = 0;
  int format = GL_RGBA;
  std::vector<uint8_t> rgba;
  if (argc >= 9) {
    width = webgl_int(argc, argv, 3, 0);
    height = webgl_int(argc, argv, 4, 0);
    const int internalformat = webgl_int(argc, argv, 2, GL_RGBA);
    format = webgl_int(argc, argv, 6, GL_RGBA);
    const int type = webgl_int(argc, argv, 7, GL_UNSIGNED_BYTE);
    const std::vector<uint8_t> pixels = webgl_bytes(xt_arg(argc, argv, 8));
    const size_t count = (size_t)width * (size_t)height;
    if (width <= 0 || height <= 0) {
      context->set_error(GL_INVALID_VALUE);
      return XT_UNDEFINED;
    }
    if (is_depth_internal(internalformat)) {
      const bool stencil = internalformat == GL_DEPTH_STENCIL ||
                           internalformat == GL_DEPTH24_STENCIL8;
      if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_FLOAT &&
          type != 0x84FA) {
        context->set_error(GL_INVALID_ENUM);
        return XT_UNDEFINED;
      }
      const size_t source_bytes = stencil ? 4 : (type == GL_UNSIGNED_SHORT ? 2 : 4);
      if (!pixels.empty() && pixels.size() < count * source_bytes) {
        context->set_error(GL_INVALID_VALUE);
        return XT_UNDEFINED;
      }
      std::vector<uint8_t> data =
          convert_depth_pixels(pixels.empty() ? nullptr : pixels.data(), (int)count, type, stencil);
      if (flip_y) flip_rows_bpp(&data, width, height, 4);
      context->tex_image_format(texture, width, height, storage_format_for(internalformat),
                                data.data());
      return XT_UNDEFINED;
    }
    if (is_float_internal(internalformat) || type == GL_FLOAT || type == GL_HALF_FLOAT) {
      const SDL_GPUTextureFormat sdl =
          is_float_internal(internalformat)
              ? storage_format_for(internalformat)
              : (type == GL_HALF_FLOAT ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
                                       : SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT);
      const bool half = sdl == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
      if (type != GL_FLOAT && type != GL_HALF_FLOAT && type != GL_UNSIGNED_BYTE) {
        context->set_error(GL_INVALID_ENUM);
        return XT_UNDEFINED;
      }
      if (!pixels.empty() &&
          pixels.size() < count * (size_t)source_components(format) * component_bytes(type)) {
        context->set_error(GL_INVALID_VALUE);
        return XT_UNDEFINED;
      }
      std::vector<uint8_t> data = convert_float_pixels(
          pixels.empty() ? nullptr : pixels.data(), (int)count, format, type, half);
      if (flip_y) flip_rows_bpp(&data, width, height, half ? 8 : 16);
      context->tex_image_format(texture, width, height, sdl, data.data());
      return XT_UNDEFINED;
    }
    if (type != GL_UNSIGNED_BYTE) {
      context->set_error(GL_INVALID_ENUM);
      return XT_UNDEFINED;
    }
    if (pixels.empty()) {
      rgba.assign((size_t)width * height * 4, 0);
    } else if (pixels.size() < (size_t)width * height * bytes_per_pixel(format)) {
      context->set_error(GL_INVALID_VALUE);
      return XT_UNDEFINED;
    } else {
      webgl_convert_pixels(pixels.data(), width * height, format, premultiply, &rgba);
    }
  } else {
    if (!webgl_read_source(xt_arg(argc, argv, 5), &width, &height, &rgba, premultiply)) {
      context->set_error(GL_INVALID_VALUE);
      return XT_UNDEFINED;
    }
    format = GL_RGBA;
  }
  if (width <= 0 || height <= 0) {
    context->set_error(GL_INVALID_VALUE);
    return XT_UNDEFINED;
  }
  if (flip_y) flip_rows_bpp(&rgba, width, height, 4);
  context->tex_image_rgba(texture, width, height, rgba.data());
  return XT_UNDEFINED;
}

METHOD(webgl_tex_sub_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) return XT_UNDEFINED;
  const int level = webgl_int(argc, argv, 1, 0);
  if (level != 0) return XT_UNDEFINED;
  bool flip_y = false;
  bool premultiply = false;
  webgl_unpack_flags(self, &flip_y, &premultiply);
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int format = GL_RGBA;
  std::vector<uint8_t> rgba;
  if (argc >= 9) {
    x = webgl_int(argc, argv, 2, 0);
    y = webgl_int(argc, argv, 3, 0);
    width = webgl_int(argc, argv, 4, 0);
    height = webgl_int(argc, argv, 5, 0);
    format = webgl_int(argc, argv, 6, GL_RGBA);
    const int type = webgl_int(argc, argv, 7, GL_UNSIGNED_BYTE);
    const std::vector<uint8_t> pixels = webgl_bytes(xt_arg(argc, argv, 8));
    const size_t count = (size_t)width * (size_t)height;
    const int current = context->texture_format(texture);
    if (current == (int)SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
        current == (int)SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT) {
      const bool stencil = current == (int)SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
      if (!pixels.empty() &&
          pixels.size() < count * (stencil ? 4 : (type == GL_UNSIGNED_SHORT ? 2 : 4))) {
        return XT_UNDEFINED;
      }
      std::vector<uint8_t> data = convert_depth_pixels(
          pixels.empty() ? nullptr : pixels.data(), (int)count, type, stencil);
      if (flip_y) flip_rows_bpp(&data, width, height, 4);
      context->tex_sub_image_format(texture, x, y, width, height, data.data());
      return XT_UNDEFINED;
    }
    if (current == (int)SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ||
        current == (int)SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT) {
      const bool half = current == (int)SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
      if (!pixels.empty() &&
          pixels.size() < count * (size_t)source_components(format) * component_bytes(type)) {
        return XT_UNDEFINED;
      }
      std::vector<uint8_t> data = convert_float_pixels(
          pixels.empty() ? nullptr : pixels.data(), (int)count, format, type, half);
      if (flip_y) flip_rows_bpp(&data, width, height, half ? 8 : 16);
      context->tex_sub_image_format(texture, x, y, width, height, data.data());
      return XT_UNDEFINED;
    }
    if (pixels.size() < count * (size_t)bytes_per_pixel(format)) return XT_UNDEFINED;
    webgl_convert_pixels(pixels.data(), width * height, format, premultiply, &rgba);
  } else {
    int source_width = 0;
    int source_height = 0;
    if (!webgl_read_source(xt_arg(argc, argv, 6), &source_width, &source_height, &rgba, premultiply)) {
      return XT_UNDEFINED;
    }
    width = source_width;
    height = source_height;
  }
  if (flip_y) flip_rows_bpp(&rgba, width, height, 4);
  context->tex_sub_image_rgba(texture, x, y, width, height, rgba.data());
  return XT_UNDEFINED;
}

METHOD(webgl_generate_mipmap) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context != nullptr) context->generate_mipmap(context->bound_texture(0));
  return XT_UNDEFINED;
}

}  // namespace domapi
