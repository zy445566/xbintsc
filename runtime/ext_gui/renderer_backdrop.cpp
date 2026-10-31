/*
 * xbintsc GUI engine — `backdrop-filter` compositor.
 *
 * The backdrop is the scene painted before the element, which only the renderer
 * sees. `drawBackdropPass` samples a copy of that scene (the caller ping-pongs a
 * scratch scene texture), applies the Gaussian and the combined colour matrix in
 * one runtime-compiled shader, and writes the result back through the element's
 * rounded border-box mask. Split from `renderer_layer.cpp` so the blend
 * compositor and this one stay independently readable.
 */

#include "renderer_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gpu_shader_api.h"
#include "perf.h"
#include "webgl_glsl.h"

namespace xtgui {
namespace renderer_detail {

namespace {

/** Compile one GLSL stage into the format this device consumes. Mirrors the
 * helper in `renderer_layer.cpp`; both shaders are compiled at run time because
 * they are not part of the precompiled WebGL blobs. */
bool prepareStage(const std::string &source, bool fragment, SDL_GPUShaderFormat formats,
                  std::vector<uint32_t> *spirv, std::string *text, const Uint8 **code,
                  size_t *code_size, std::string *entry, std::string *log) {
  const GpuShaderApi *api = gpu_shader_api();
  if (api == nullptr) {
    *log = "shader translation was not linked (the 'gpu' link feature is inactive)";
    return false;
  }
  GlslShaderInfo info;
  api->scan_glsl(source, fragment, &info);
  if (!info.unsupported.empty()) {
    *log = info.unsupported;
    return false;
  }
  GlslLinkLayout layout;
  layout.varyings["v_uv"] = 0;
  if (fragment) {
    layout.samplers["u_backdrop"] = 0;
    layout.block_members = info.uniforms;
  }
  const std::string rewritten = api->rewrite_glsl(source, fragment, layout, info);
  if (!api->compile_glsl(rewritten, fragment, spirv, log)) return false;

  if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
    if (!api->cross_compile_msl(*spirv, fragment, text, entry, log)) return false;
    *code = (const Uint8 *)text->data();
    *code_size = text->size();
    return true;
  }
  if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
    *code = (const Uint8 *)spirv->data();
    *code_size = spirv->size() * sizeof(uint32_t);
    *entry = "main";
    return true;
  }
  *log = "this GPU backend consumes neither Metal nor Vulkan shaders";
  return false;
}

// `v_uv` comes from the shared full-screen vertex shader.
const char *const kBlendVS = R"GLSL(#version 300 es
precision highp float;

out vec2 v_uv;

void main() {
  vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
                            vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
  vec2 p = corners[gl_VertexIndex];
  v_uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
  gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

/* `backdrop-filter`: samples the scene painted so far with a separable-looking
 * 13x13 Gaussian (taps spread to cover sigma) and one combined colour matrix,
 * then masks the result to the element's rounded border box. The scene has
 * already been copied to the sampled texture by a copy pass, so this shader
 * never reads the target it writes. */
const char *const kBackdropFS = R"GLSL(#version 300 es
precision highp float;

in vec2 v_uv;
uniform sampler2D u_backdrop;
uniform vec4 u_rect;     // x, y, width, height in target pixels
uniform vec4 u_mask;     // corner radius, sigma, target width, target height
uniform vec4 u_texel;    // 1/target width, 1/target height
uniform vec4 u_row0;     // colour matrix row 0 + offset
uniform vec4 u_row1;
uniform vec4 u_row2;
uniform vec4 u_extra;    // alpha multiplier
out vec4 fragColor;

float roundedRectDistance(vec2 point, vec4 rect, float radius) {
  vec2 half_size = rect.zw * 0.5;
  vec2 centre = rect.xy + half_size;
  radius = min(radius, min(half_size.x, half_size.y));
  vec2 q = abs(point - centre) - (half_size - vec2(radius));
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

void main() {
  float sigma = u_mask.y;
  vec4 sampled;
  if (sigma > 0.05) {
    float step = max(1.0, sigma * 3.0 / 6.0);
    float total = 0.0;
    vec4 sum = vec4(0.0);
    for (int y = -6; y <= 6; y++) {
      for (int x = -6; x <= 6; x++) {
        float distance2 = float(x * x + y * y) * step * step;
        float weight = exp(-distance2 / (2.0 * sigma * sigma));
        vec4 texel = texture(u_backdrop, v_uv + vec2(float(x), float(y)) * step * u_texel.xy);
        /* Premultiply before averaging: blurring straight alpha would pull
         * transparent black into the edges of a shape. */
        sum += vec4(texel.rgb * texel.a, texel.a) * weight;
        total += weight;
      }
    }
    vec4 blended = sum / total;
    sampled = vec4(blended.a > 0.00001 ? blended.rgb / blended.a : vec3(0.0), blended.a);
  } else {
    sampled = texture(u_backdrop, v_uv);
  }
  vec3 rgb = sampled.rgb;
  vec3 filtered = vec3(dot(u_row0.xyz, rgb) + u_row0.w,
                       dot(u_row1.xyz, rgb) + u_row1.w,
                       dot(u_row2.xyz, rgb) + u_row2.w);
  filtered = clamp(filtered, 0.0, 1.0);
  vec2 position = vec2(v_uv.x * u_mask.z, v_uv.y * u_mask.w);
  float distance = roundedRectDistance(position, u_rect, u_mask.x);
  float coverage = clamp(0.5 - distance, 0.0, 1.0);
  if (coverage <= 0.0) discard;
  fragColor = vec4(filtered, clamp(sampled.a, 0.0, 1.0) * u_extra.x * coverage);
}
)GLSL";

}  // namespace

SDL_GPUGraphicsPipeline *g_backdrop_pipeline = nullptr;
SDL_GPUSampler *g_backdrop_sampler = nullptr;
bool g_backdrop_failed = false;

bool ensureBackdropPipeline(SDL_GPUDevice *device) {
  if (g_backdrop_pipeline != nullptr) return true;
  if (g_backdrop_failed) return false;
  if (device == nullptr || g_swapchain_format == SDL_GPU_TEXTUREFORMAT_INVALID) return false;
  const SDL_GPUTextureUsageFlags usage =
      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  if (!SDL_GPUTextureSupportsFormat(device, g_swapchain_format, SDL_GPU_TEXTURETYPE_2D, usage)) {
    g_backdrop_failed = true;
    return false;
  }
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
  if ((formats & (SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV)) == 0) {
    g_backdrop_failed = true;
    return false;
  }

  std::string log;
  std::vector<uint32_t> vs_spirv;
  std::vector<uint32_t> fs_spirv;
  std::string vs_text;
  std::string fs_text;
  const Uint8 *vs_code = nullptr;
  const Uint8 *fs_code = nullptr;
  size_t vs_size = 0;
  size_t fs_size = 0;
  std::string vs_entry;
  std::string fs_entry;
  if (!prepareStage(kBlendVS, false, formats, &vs_spirv, &vs_text, &vs_code, &vs_size, &vs_entry,
                    &log) ||
      !prepareStage(kBackdropFS, true, formats, &fs_spirv, &fs_text, &fs_code, &fs_size,
                    &fs_entry, &log)) {
    fprintf(stderr, "xt_gui: backdrop-filter shader unavailable: %s\n", log.c_str());
    g_backdrop_failed = true;
    return false;
  }

  const SDL_GPUShaderFormat shader_format =
      (formats & SDL_GPU_SHADERFORMAT_MSL) != 0 ? SDL_GPU_SHADERFORMAT_MSL
                                                : SDL_GPU_SHADERFORMAT_SPIRV;
  SDL_GPUShaderCreateInfo vs_info;
  std::memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = vs_size;
  vs_info.code = vs_code;
  vs_info.entrypoint = vs_entry.c_str();
  vs_info.format = shader_format;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  std::memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = fs_size;
  fs_info.code = fs_code;
  fs_info.entrypoint = fs_entry.c_str();
  fs_info.format = shader_format;
  fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fs_info.num_uniform_buffers = 1;
  fs_info.num_samplers = 1;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: backdrop-filter shader creation failed: %s\n", SDL_GetError());
    if (vs != nullptr) SDL_ReleaseGPUShader(device, vs);
    if (fs != nullptr) SDL_ReleaseGPUShader(device, fs);
    g_backdrop_failed = true;
    return false;
  }

  SDL_GPUColorTargetDescription target;
  std::memset(&target, 0, sizeof(target));
  target.format = g_swapchain_format;
  target.blend_state.enable_blend = true;
  target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo info;
  std::memset(&info, 0, sizeof(info));
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  g_backdrop_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
  SDL_ReleaseGPUShader(device, vs);
  SDL_ReleaseGPUShader(device, fs);
  if (g_backdrop_pipeline == nullptr) {
    fprintf(stderr, "xt_gui: backdrop-filter pipeline creation failed: %s\n", SDL_GetError());
    g_backdrop_failed = true;
    return false;
  }

  SDL_GPUSamplerCreateInfo sampler_info;
  std::memset(&sampler_info, 0, sizeof(sampler_info));
  sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
  sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g_backdrop_sampler = SDL_CreateGPUSampler(device, &sampler_info);
  if (g_backdrop_sampler == nullptr) {
    fprintf(stderr, "xt_gui: backdrop-filter sampler creation failed: %s\n", SDL_GetError());
    SDL_ReleaseGPUGraphicsPipeline(device, g_backdrop_pipeline);
    g_backdrop_pipeline = nullptr;
    g_backdrop_failed = true;
    return false;
  }
  return true;
}

struct BackdropParams {
  float rect[4];
  float mask[4];
  float texel[4];
  float row0[4];
  float row1[4];
  float row2[4];
  float extra[4];
};

/** Filter `source` (the scene painted so far) into the active target only
 * inside the element's rounded rect. The target already holds the same scene,
 * so the fragment's source-over blend replaces the region with the filtered
 * version while leaving rounded-corner coverage to antialias the edge. */
void drawBackdropPass(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                      SDL_GPUTexture *source, const DisplayList &list,
                      const XtGuiDrawRun &run, int target_width, int target_height,
                      float pixel_density) {
  if (cmd == nullptr || pass == nullptr || source == nullptr ||
      g_backdrop_pipeline == nullptr || g_backdrop_sampler == nullptr)
    return;
  if (run.backdrop >= list.backdrops.size()) return;
  const xtgui::PaintBackdrop &backdrop = list.backdrops[run.backdrop];
  if (backdrop.width <= 0.0f || backdrop.height <= 0.0f) return;

  const float density = pixel_density;
  float left = backdrop.x * density;
  float top = backdrop.y * density;
  float right = (backdrop.x + backdrop.width) * density;
  float bottom = (backdrop.y + backdrop.height) * density;
  if (run.clip_width > 0.0f && run.clip_height > 0.0f) {
    left = std::max(left, run.clip_x * density);
    top = std::max(top, run.clip_y * density);
    right = std::min(right, (run.clip_x + run.clip_width) * density);
    bottom = std::min(bottom, (run.clip_y + run.clip_height) * density);
  }
  int x0 = (int)std::floor(left);
  int y0 = (int)std::floor(top);
  int x1 = (int)std::ceil(right);
  int y1 = (int)std::ceil(bottom);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > target_width) x1 = target_width;
  if (y1 > target_height) y1 = target_height;
  if (x1 <= x0 || y1 <= y0) return;

  SDL_BindGPUGraphicsPipeline(pass, g_backdrop_pipeline);
  SDL_GPUTextureSamplerBinding binding;
  binding.texture = source;
  binding.sampler = g_backdrop_sampler;
  SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

  BackdropParams params;
  std::memset(&params, 0, sizeof(params));
  params.rect[0] = backdrop.x * density;
  params.rect[1] = backdrop.y * density;
  params.rect[2] = backdrop.width * density;
  params.rect[3] = backdrop.height * density;
  params.mask[0] = backdrop.radius * density;
  params.mask[1] = backdrop.blur * density;
  params.mask[2] = (float)target_width;
  params.mask[3] = (float)target_height;
  params.texel[0] = 1.0f / (float)target_width;
  params.texel[1] = 1.0f / (float)target_height;
  for (int i = 0; i < 3; i++) {
    params.row0[i] = backdrop.color[i];
    params.row1[i] = backdrop.color[3 + i];
    params.row2[i] = backdrop.color[6 + i];
  }
  params.row0[3] = backdrop.offset[0];
  params.row1[3] = backdrop.offset[1];
  params.row2[3] = backdrop.offset[2];
  params.extra[0] = backdrop.alpha;
  SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));

  SDL_Rect scissor;
  scissor.x = x0;
  scissor.y = y0;
  scissor.w = x1 - x0;
  scissor.h = y1 - y0;
  SDL_SetGPUScissor(pass, &scissor);
  SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
  xtgui::g_perf_frame.drawCalls += 1;
}


bool backdropRendererReady(SDL_GPUDevice *device) { return ensureBackdropPipeline(device); }

void backdropRendererDestroy(SDL_GPUDevice *device) {
  if (g_backdrop_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_backdrop_pipeline);
    g_backdrop_pipeline = nullptr;
  }
  if (g_backdrop_sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, g_backdrop_sampler);
    g_backdrop_sampler = nullptr;
  }
  g_backdrop_failed = false;
}

}  // namespace renderer_detail
}  // namespace xtgui
