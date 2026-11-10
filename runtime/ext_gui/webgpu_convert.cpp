/*
 * xbintsc GUI engine — conversions and JS argument helpers for WebGPU.
 */

#include "webgpu.h"

#include <cstring>

namespace xtgui {
namespace webgpu {

/* -- argument helpers ----------------------------------------------------- */

std::string arg_string(int argc, xt_value *argv, int index) {
  xt_value value = xt_arg(argc, argv, index);
  if (XT_IS_UNDEFINED(value) || XT_IS_NULL(value)) return std::string();
  return xt_string_data(xt_to_string(value));
}

double arg_number(int argc, xt_value *argv, int index) {
  return xt_to_number(xt_arg(argc, argv, index));
}

uint64_t arg_u64(int argc, xt_value *argv, int index) {
  const double value = arg_number(argc, argv, index);
  return value > 0 ? (uint64_t)value : 0;
}

bool arg_bool(int argc, xt_value *argv, int index, bool fallback) {
  xt_value value = xt_arg(argc, argv, index);
  if (XT_IS_UNDEFINED(value)) return fallback;
  return xt_truthy(value) != 0;
}

xt_value arg_object(int argc, xt_value *argv, int index) {
  xt_value value = xt_arg(argc, argv, index);
  return (XT_IS_OBJECT(value) || XT_IS_ARRAY(value)) ? value : XT_UNDEFINED;
}

std::string prop_string(xt_value object, const char *name) {
  if (!XT_IS_OBJECT(object)) return std::string();
  xt_value value = xt_object_get_cstr(object, name);
  if (XT_IS_UNDEFINED(value)) return std::string();
  return xt_string_data(xt_to_string(value));
}

double prop_number(xt_value object, const char *name, double fallback) {
  if (!XT_IS_OBJECT(object)) return fallback;
  xt_value value = xt_object_get_cstr(object, name);
  if (!XT_IS_NUMBER(value)) return fallback;
  return xt_to_number(value);
}

uint64_t prop_u64(xt_value object, const char *name, uint64_t fallback) {
  const double value = prop_number(object, name, (double)fallback);
  return value >= 0 ? (uint64_t)value : fallback;
}

bool prop_bool(xt_value object, const char *name, bool fallback) {
  if (!XT_IS_OBJECT(object)) return fallback;
  xt_value value = xt_object_get_cstr(object, name);
  if (XT_IS_UNDEFINED(value)) return fallback;
  return xt_truthy(value) != 0;
}

std::string prop_name(xt_value object, const char *name) {
  return prop_string(object, name);
}

xt_value prop_object(xt_value object, const char *name) {
  if (!XT_IS_OBJECT(object)) return XT_UNDEFINED;
  xt_value value = xt_object_get_cstr(object, name);
  /* Arrays are a separate tag in the runtime but are JS objects here. */
  return (XT_IS_OBJECT(value) || XT_IS_ARRAY(value)) ? value : XT_UNDEFINED;
}

bool read_bytes(xt_value value, std::vector<uint8_t> *out) {
  if (!XT_IS_OBJECT(value)) return false;
  const int kind = xt_typed_array_kind_of(value);
  if (kind >= 0) {
    static const uint32_t kSizes[] = {1, 1, 1, 2, 2, 4, 4, 4, 8};
    const uint32_t element = kind < 9 ? kSizes[kind] : 1;
    const uint32_t count = xt_typed_array_length(value) * element;
    xt_value buffer = xt_typed_array_backing_buffer(value);
    const uint32_t offset = xt_typed_array_view_offset(value);
    out->resize(count);
    if (count == 0) return true;
    return xt_array_buffer_copy_bytes_out(buffer, offset, out->data(), count) == count;
  }
  /* ArrayBuffer / DataView. */
  xt_value length_value = xt_object_get_cstr(value, "byteLength");
  if (!XT_IS_NUMBER(length_value)) return false;
  const uint32_t length = (uint32_t)xt_to_number(length_value);
  out->resize(length);
  if (length == 0) return true;
  return xt_array_buffer_copy_bytes_out(value, 0, out->data(), length) == length;
}

/* -- texture formats ------------------------------------------------------ */

bool Convert::texture_format(const std::string &name, SDL_GPUTextureFormat *out) {
  static const struct {
    const char *name;
    SDL_GPUTextureFormat format;
  } kFormats[] = {
      {"r8unorm", SDL_GPU_TEXTUREFORMAT_R8_UNORM},
      {"r8snorm", SDL_GPU_TEXTUREFORMAT_R8_SNORM},
      {"r8uint", SDL_GPU_TEXTUREFORMAT_R8_UINT},
      {"r8sint", SDL_GPU_TEXTUREFORMAT_R8_INT},
      {"rg8unorm", SDL_GPU_TEXTUREFORMAT_R8G8_UNORM},
      {"rg8snorm", SDL_GPU_TEXTUREFORMAT_R8G8_SNORM},
      {"rg8uint", SDL_GPU_TEXTUREFORMAT_R8G8_UINT},
      {"rg8sint", SDL_GPU_TEXTUREFORMAT_R8G8_INT},
      {"rgba8unorm", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM},
      {"rgba8unorm-srgb", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB},
      {"rgba8snorm", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM},
      {"rgba8uint", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT},
      {"rgba8sint", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT},
      {"bgra8unorm", SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM},
      {"bgra8unorm-srgb", SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB},
      {"r16float", SDL_GPU_TEXTUREFORMAT_R16_FLOAT},
      {"rg16float", SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT},
      {"rgba16float", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT},
      {"r32float", SDL_GPU_TEXTUREFORMAT_R32_FLOAT},
      {"rg32float", SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT},
      {"rgba32float", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT},
      {"r32uint", SDL_GPU_TEXTUREFORMAT_R32_UINT},
      {"rg32uint", SDL_GPU_TEXTUREFORMAT_R32G32_UINT},
      {"rgba32uint", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT},
      {"r32sint", SDL_GPU_TEXTUREFORMAT_R32_INT},
      {"rgba16uint", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT},
      {"rgba16sint", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT},
      {"depth16unorm", SDL_GPU_TEXTUREFORMAT_D16_UNORM},
      /* WebGPU's `depth24plus` is a promise format: the implementation picks a
       * depth of at least 24 bits. Metal has no 24-bit-only depth format (its
       * `D24_UNORM` is `MTLPixelFormatInvalid`), so use 32-bit float, which is
       * ≥24 bits and supported by every backend (matching Dawn on Metal). */
      {"depth24plus", SDL_GPU_TEXTUREFORMAT_D32_FLOAT},
      {"depth24plus-stencil8", SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT},
      {"depth32float", SDL_GPU_TEXTUREFORMAT_D32_FLOAT},
      {"depth32float-stencil8", SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT},
  };
  for (const auto &candidate : kFormats) {
    if (name == candidate.name) {
      *out = candidate.format;
      return true;
    }
  }
  return false;
}

const char *Convert::texture_format_name(SDL_GPUTextureFormat format) {
  switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return "rgba8unorm";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return "bgra8unorm";
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return "rgba8unorm-srgb";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return "bgra8unorm-srgb";
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT: return "r32float";
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT: return "rgba32float";
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM: return "depth16unorm";
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM: return "depth24plus";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT: return "depth32float";
    default: return "rgba8unorm";
  }
}

bool Convert::vertex_format(const std::string &name, SDL_GPUVertexElementFormat *out) {
  static const struct {
    const char *name;
    SDL_GPUVertexElementFormat format;
  } kFormats[] = {
      {"float32", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT},
      {"float32x2", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2},
      {"float32x3", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3},
      {"float32x4", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4},
      {"uint32", SDL_GPU_VERTEXELEMENTFORMAT_UINT},
      {"uint32x2", SDL_GPU_VERTEXELEMENTFORMAT_UINT2},
      {"uint32x3", SDL_GPU_VERTEXELEMENTFORMAT_UINT3},
      {"uint32x4", SDL_GPU_VERTEXELEMENTFORMAT_UINT4},
      {"sint32", SDL_GPU_VERTEXELEMENTFORMAT_INT},
      {"sint32x2", SDL_GPU_VERTEXELEMENTFORMAT_INT2},
      {"sint32x3", SDL_GPU_VERTEXELEMENTFORMAT_INT3},
      {"sint32x4", SDL_GPU_VERTEXELEMENTFORMAT_INT4},
      {"unorm8x4", SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM},
      {"snorm8x4", SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM},
      {"unorm16x2", SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM},
      {"unorm16x4", SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM},
      {"float16x2", SDL_GPU_VERTEXELEMENTFORMAT_HALF2},
      {"float16x4", SDL_GPU_VERTEXELEMENTFORMAT_HALF4},
  };
  for (const auto &candidate : kFormats) {
    if (name == candidate.name) {
      *out = candidate.format;
      return true;
    }
  }
  return false;
}

SDL_GPUPrimitiveType Convert::topology(const std::string &name) {
  if (name == "line-list") return SDL_GPU_PRIMITIVETYPE_LINELIST;
  if (name == "line-strip") return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
  if (name == "point-list") return SDL_GPU_PRIMITIVETYPE_POINTLIST;
  if (name == "triangle-strip") return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
  return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

SDL_GPUCompareOp Convert::compare(const std::string &name) {
  if (name == "never") return SDL_GPU_COMPAREOP_NEVER;
  if (name == "less") return SDL_GPU_COMPAREOP_LESS;
  if (name == "equal") return SDL_GPU_COMPAREOP_EQUAL;
  if (name == "less-equal") return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  if (name == "greater") return SDL_GPU_COMPAREOP_GREATER;
  if (name == "not-equal") return SDL_GPU_COMPAREOP_NOT_EQUAL;
  if (name == "greater-equal") return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
  return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPUBlendFactor Convert::blend_factor(const std::string &name) {
  if (name == "zero") return SDL_GPU_BLENDFACTOR_ZERO;
  if (name == "one") return SDL_GPU_BLENDFACTOR_ONE;
  if (name == "src") return SDL_GPU_BLENDFACTOR_SRC_COLOR;
  if (name == "one-minus-src") return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
  if (name == "dst") return SDL_GPU_BLENDFACTOR_DST_COLOR;
  if (name == "one-minus-dst") return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
  if (name == "src-alpha") return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  if (name == "one-minus-src-alpha") return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  if (name == "dst-alpha") return SDL_GPU_BLENDFACTOR_DST_ALPHA;
  if (name == "one-minus-dst-alpha") return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
  if (name == "constant") return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
  if (name == "one-minus-constant") return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
  if (name == "src-alpha-saturated") return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
  return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUBlendOp Convert::blend_op(const std::string &name) {
  if (name == "subtract") return SDL_GPU_BLENDOP_SUBTRACT;
  if (name == "reverse-subtract") return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
  if (name == "min") return SDL_GPU_BLENDOP_MIN;
  if (name == "max") return SDL_GPU_BLENDOP_MAX;
  return SDL_GPU_BLENDOP_ADD;
}

SDL_GPUSamplerAddressMode Convert::address_mode(const std::string &name) {
  if (name == "repeat") return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  if (name == "mirror-repeat") return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
  /* WebGPU defaults to clamp-to-edge. */
  return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

SDL_GPUFilter Convert::filter(const std::string &name) {
  return name == "linear" ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
}

SDL_GPUSamplerMipmapMode Convert::mipmap_mode(const std::string &name) {
  return name == "linear" ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
}

uint32_t Convert::texture_usage(uint32_t usage, SDL_GPUTextureFormat format) {
  uint32_t flags = 0;
  if ((usage & 4) != 0) flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
  if ((usage & 16) != 0) {
    flags |= is_depth_format(format) ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                     : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  }
  if ((usage & 8) != 0) {
    flags |= SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ |
             SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
             SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
  }
  return flags;
}

uint32_t Convert::bytes_per_pixel(SDL_GPUTextureFormat format) {
  switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8_UINT:
    case SDL_GPU_TEXTUREFORMAT_R8_INT: return 1;
    case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8_UINT:
    case SDL_GPU_TEXTUREFORMAT_R8G8_INT:
    case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM: return 2;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
    case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32_UINT:
    case SDL_GPU_TEXTUREFORMAT_R32_INT:
    case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT: return 4;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT: return 8;
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT: return 16;
    default: return 4;
  }
}

bool Convert::is_depth_format(SDL_GPUTextureFormat format) {
  return format == SDL_GPU_TEXTUREFORMAT_D16_UNORM || format == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
         format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
         format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

}  // namespace webgpu
}  // namespace xtgui
