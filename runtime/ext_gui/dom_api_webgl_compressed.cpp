/*
 * xbintsc GUI engine — compressed-texture WebGL bindings.
 *
 * `getExtension` publishes the S3TC/ASTC enum objects only when the device can
 * sample the format, and `compressedTexImage2D` / `compressedTexSubImage2D`
 * hand the tightly packed blocks to `webgl_compressed.cpp`.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

#include "webgl.h"

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

namespace domapi {
namespace {

xtgui::WebGLContext *context_of(xt_value self) { return webgl_live_context(self); }

uint32_t texture_argument(xt_value value) { return webgl_object_id(value, "__xt_gl_texture"); }

std::vector<uint8_t> compressed_bytes(int32_t argc, xt_value *argv, int32_t data_index,
                                      int32_t offset_index, int32_t length_index) {
  std::vector<uint8_t> bytes = webgl_bytes(xt_arg(argc, argv, data_index));
  const uint32_t offset = (uint32_t)webgl_number(argc, argv, offset_index, 0.0);
  uint32_t length = (uint32_t)webgl_number(argc, argv, length_index, (double)bytes.size());
  if (offset > bytes.size()) return std::vector<uint8_t>();
  if (length > bytes.size() - offset) length = (uint32_t)bytes.size() - offset;
  return std::vector<uint8_t>(bytes.begin() + offset, bytes.begin() + offset + length);
}

void add_constant(xt_value object, const char *name, int value) {
  xt_object_set(object, xt_string_from_cstr(name), xt_number((double)value));
}

}  // namespace

METHOD(webgl_compressed_tex_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) return XT_UNDEFINED;
  const int level = webgl_int(argc, argv, 1, 0);
  if (level != 0) return XT_UNDEFINED;
  const int internalformat = webgl_int(argc, argv, 2, 0);
  const SDL_GPUTextureFormat format = xtgui::webgl_compressed_format(internalformat);
  if (format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    context->set_error(GL_INVALID_ENUM);
    return XT_UNDEFINED;
  }
  const int width = webgl_int(argc, argv, 3, 0);
  const int height = webgl_int(argc, argv, 4, 0);
  const std::vector<uint8_t> data = compressed_bytes(argc, argv, 6, 7, 8);
  context->tex_image_compressed(texture, width, height, format, 0, 0, width, height,
                                data.empty() ? nullptr : data.data(), (uint32_t)data.size());
  return XT_UNDEFINED;
}

METHOD(webgl_compressed_tex_sub_image_2d) {
  (void)env;
  xtgui::WebGLContext *context = context_of(self);
  if (context == nullptr) return XT_UNDEFINED;
  const uint32_t texture = context->bound_texture(0);
  if (texture == 0) return XT_UNDEFINED;
  const int internalformat = webgl_int(argc, argv, 6, 0);
  const SDL_GPUTextureFormat format = xtgui::webgl_compressed_format(internalformat);
  if (format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    context->set_error(GL_INVALID_ENUM);
    return XT_UNDEFINED;
  }
  const int x = webgl_int(argc, argv, 2, 0);
  const int y = webgl_int(argc, argv, 3, 0);
  const int width = webgl_int(argc, argv, 4, 0);
  const int height = webgl_int(argc, argv, 5, 0);
  const std::vector<uint8_t> data = compressed_bytes(argc, argv, 7, 8, 9);
  context->tex_image_compressed(texture, context->texture_width(texture),
                                context->texture_height(texture), format, x, y, width, height,
                                data.empty() ? nullptr : data.data(), (uint32_t)data.size());
  return XT_UNDEFINED;
}

/** `getExtension` for the compressed-texture families; returns null when the
 * device cannot sample the representative format, and undefined when `name` is
 * not a compressed-texture extension. */
xt_value webgl_compressed_extension(const std::string &name, xtgui::WebGLContext *context) {
  struct CompressedConstant {
    const char *name;
    int value;
  };
  static const CompressedConstant kS3tc[] = {
      {"COMPRESSED_RGB_S3TC_DXT1_EXT", 0x83F0},
      {"COMPRESSED_RGBA_S3TC_DXT1_EXT", 0x83F1},
      {"COMPRESSED_RGBA_S3TC_DXT3_EXT", 0x83F2},
      {"COMPRESSED_RGBA_S3TC_DXT5_EXT", 0x83F3},
  };
  static const CompressedConstant kS3tcSrgb[] = {
      {"COMPRESSED_SRGB_S3TC_DXT1_EXT", 0x8C4C},
      {"COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT", 0x8C4D},
      {"COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT", 0x8C4E},
      {"COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT", 0x8C4F},
  };
  static const CompressedConstant kAstc[] = {
      {"COMPRESSED_RGBA_ASTC_4x4_KHR", 0x93B0},
      {"COMPRESSED_RGBA_ASTC_5x4_KHR", 0x93B1},
      {"COMPRESSED_RGBA_ASTC_5x5_KHR", 0x93B2},
      {"COMPRESSED_RGBA_ASTC_6x5_KHR", 0x93B3},
      {"COMPRESSED_RGBA_ASTC_6x6_KHR", 0x93B4},
      {"COMPRESSED_RGBA_ASTC_8x5_KHR", 0x93B5},
      {"COMPRESSED_RGBA_ASTC_8x6_KHR", 0x93B6},
      {"COMPRESSED_RGBA_ASTC_8x8_KHR", 0x93B7},
      {"COMPRESSED_RGBA_ASTC_10x5_KHR", 0x93B8},
      {"COMPRESSED_RGBA_ASTC_10x6_KHR", 0x93B9},
      {"COMPRESSED_RGBA_ASTC_10x8_KHR", 0x93BA},
      {"COMPRESSED_RGBA_ASTC_10x10_KHR", 0x93BB},
      {"COMPRESSED_RGBA_ASTC_12x10_KHR", 0x93BC},
      {"COMPRESSED_RGBA_ASTC_12x12_KHR", 0x93BD},
  };
  static const CompressedConstant kAstcSrgb[] = {
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR", 0x93D0},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR", 0x93D1},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR", 0x93D2},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR", 0x93D3},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR", 0x93D4},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR", 0x93D5},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR", 0x93D6},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR", 0x93D7},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR", 0x93D8},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR", 0x93D9},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR", 0x93DA},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR", 0x93DB},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR", 0x93DC},
      {"COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR", 0x93DD},
  };
  if (context == nullptr) return XT_NULL;
  xt_value object = xt_object_new();
  int representative = 0;
  const CompressedConstant *table = kS3tc;
  size_t count = sizeof(kS3tc) / sizeof(kS3tc[0]);
  if (name == "WEBGL_compressed_texture_s3tc") {
    representative = 0x83F1;
  } else if (name == "WEBGL_compressed_texture_s3tc_srgb") {
    table = kS3tcSrgb;
    count = sizeof(kS3tcSrgb) / sizeof(kS3tcSrgb[0]);
    representative = 0x8C4D;
  } else if (name == "WEBGL_compressed_texture_astc") {
    table = kAstc;
    count = sizeof(kAstc) / sizeof(kAstc[0]);
    representative = 0x93B0;
  } else {
    return XT_UNDEFINED;
  }
  if (!context->supports_texture_format(representative)) return XT_NULL;
  for (size_t i = 0; i < count; i++) add_constant(object, table[i].name, table[i].value);
  if (name == "WEBGL_compressed_texture_astc") {
    /* The ASTC extension publishes both LDR and sRGB enums. */
    for (size_t i = 0; i < sizeof(kAstcSrgb) / sizeof(kAstcSrgb[0]); i++) {
      add_constant(object, kAstcSrgb[i].name, kAstcSrgb[i].value);
    }
  }
  return object;
}

}  // namespace domapi
