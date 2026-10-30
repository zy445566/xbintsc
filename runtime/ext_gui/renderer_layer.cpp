/*
 * xbintsc GUI engine — offscreen `mix-blend-mode` compositing.
 *
 * The ordinary painter is single-pass: every shape blends directly with the
 * swapchain, which is fine for source-over but cannot sample the backdrop that
 * a CSS blend mode needs. A `mix-blend-mode` element is therefore painted into
 * a display-list group (see `paint.cpp`), rendered to an offscreen texture and
 * composited over the scene with a small GLSL shader. The shader is compiled at
 * run time by the bundled glslang and, on Metal, cross-compiled to MSL by
 * SPIRV-Cross — the same path WebGL uses. Backends that consume neither format
 * (D3D12/DXIL) fall back to the ordinary painter.
 *
 * Two full-viewport textures per nesting level are reused across frames; one
 * holds the current scene, the other is the target of the next composite. That
 * ping-pong keeps the backdrop sampleable while it is being written.
 */

#include "renderer_internal.h"

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

/* `u_mode` is the MixBlendMode ordinal, or -1 to copy the backdrop (the final
 * blit to the swapchain). Colours are un-premultiplied for the blend formula
 * and re-premultiplied for the source-over composite, matching the CSS
 * compositing spec. */
const char *const kBlendFS = R"GLSL(#version 300 es
precision highp float;

in vec2 v_uv;
uniform sampler2D u_backdrop;
uniform sampler2D u_layer;
uniform int u_mode;
out vec4 fragColor;

float separable(int mode, float b, float s) {
  if (mode == 1) return b * s;
  if (mode == 2) return b + s - b * s;
  if (mode == 3) return b <= 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if (mode == 4) return min(b, s);
  if (mode == 5) return max(b, s);
  if (mode == 6) return b <= 0.0 ? 0.0 : (s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s)));
  if (mode == 7) return b >= 1.0 ? 1.0 : (s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b) / s));
  if (mode == 8) return s <= 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if (mode == 9) {
    float d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
    return s <= 0.5 ? b - (1.0 - 2.0 * s) * b * (1.0 - b) : b + (2.0 * s - 1.0) * (d - b);
  }
  if (mode == 10) return abs(b - s);
  if (mode == 11) return b + s - 2.0 * b * s;
  return s;
}

float lum(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }

vec3 clipColor(vec3 c) {
  float l = lum(c);
  float n = min(min(c.r, c.g), c.b);
  float x = max(max(c.r, c.g), c.b);
  if (n < 0.0) c = l + (c - l) * l / (l - n);
  if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l);
  return c;
}

vec3 setLum(vec3 c, float l) { return clipColor(c + (l - lum(c))); }

float sat(vec3 c) { return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b); }

vec3 setSat(vec3 c, float s) {
  float mn = min(min(c.r, c.g), c.b);
  float mx = max(max(c.r, c.g), c.b);
  if (mx <= mn) return vec3(0.0);
  return (c - mn) * s / (mx - mn);
}

vec3 nonSeparable(int mode, vec3 b, vec3 s) {
  if (mode == 12) return setLum(setSat(s, sat(b)), lum(b));
  if (mode == 13) return setLum(setSat(b, sat(s)), lum(b));
  if (mode == 14) return setLum(s, lum(b));
  if (mode == 15) return setLum(b, lum(s));
  return s;
}

void main() {
  vec4 b = texture(u_backdrop, v_uv);
  if (u_mode < 0) {
    fragColor = b;
    return;
  }
  vec4 s = texture(u_layer, v_uv);
  float ab = clamp(b.a, 0.0, 1.0);
  float as = clamp(s.a, 0.0, 1.0);
  vec3 cb = ab > 0.00001 ? b.rgb / ab : vec3(0.0);
  vec3 cs = as > 0.00001 ? s.rgb / as : vec3(0.0);
  vec3 blended;
  if (u_mode <= 11) {
    blended = vec3(separable(u_mode, cb.r, cs.r), separable(u_mode, cb.g, cs.g),
                   separable(u_mode, cb.b, cs.b));
  } else {
    blended = nonSeparable(u_mode, cb, cs);
  }
  float ao = as + ab * (1.0 - as);
  vec3 premul = as * (1.0 - ab) * cs + as * ab * blended + (1.0 - as) * ab * cb;
  vec3 co = ao > 0.00001 ? premul / ao : vec3(0.0);
  fragColor = vec4(clamp(co, 0.0, 1.0), ao);
}
)GLSL";

SDL_GPUGraphicsPipeline *g_layer_pipeline = nullptr;
SDL_GPUSampler *g_layer_sampler = nullptr;
SDL_GPUTextureFormat g_layer_format = SDL_GPU_TEXTUREFORMAT_INVALID;
bool g_layer_failed = false;

struct BlendParams {
  int mode;
  int padding[3];
};

void pushMode(SDL_GPUCommandBuffer *cmd, int mode) {
  BlendParams params;
  std::memset(&params, 0, sizeof(params));
  params.mode = mode;
  SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));
}

/** Compile `source` into the shader format the device consumes. The returned
 * `code` either points at `spirv` or at `text`, so both must outlive
 * `SDL_CreateGPUShader`. */
bool prepareStage(const std::string &source, bool fragment, SDL_GPUShaderFormat formats,
                  std::vector<uint32_t> *spirv, std::string *text, const Uint8 **code,
                  size_t *code_size, std::string *entry, std::string *log) {
  const GpuShaderApi *api = gpu_shader_api();
  if (api == nullptr) {
    *log = "shader translation was not linked (the 'gpu' link feature is inactive)";
    return false;
  }
  GlslShaderInfo info;
  scan_glsl(source, fragment, &info);
  if (!info.unsupported.empty()) {
    *log = info.unsupported;
    return false;
  }
  GlslLinkLayout layout;
  layout.varyings["v_uv"] = 0;
  if (fragment) {
    layout.samplers["u_backdrop"] = 0;
    layout.samplers["u_layer"] = 1;
    layout.block_members = info.uniforms;
  }
  const std::string rewritten = rewrite_glsl(source, fragment, layout, info);
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

bool ensureLayerPipeline(SDL_GPUDevice *device) {
  if (g_layer_pipeline != nullptr) return true;
  if (g_layer_failed) return false;
  if (device == nullptr || g_swapchain_format == SDL_GPU_TEXTUREFORMAT_INVALID) return false;
  const SDL_GPUTextureUsageFlags usage =
      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  if (!SDL_GPUTextureSupportsFormat(device, g_swapchain_format, SDL_GPU_TEXTURETYPE_2D, usage)) {
    g_layer_failed = true;
    return false;
  }
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
  if ((formats & (SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV)) == 0) {
    g_layer_failed = true;
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
      !prepareStage(kBlendFS, true, formats, &fs_spirv, &fs_text, &fs_code, &fs_size, &fs_entry,
                    &log)) {
    fprintf(stderr, "xt_gui: mix-blend-mode shader unavailable: %s\n", log.c_str());
    g_layer_failed = true;
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
  fs_info.num_samplers = 2;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: mix-blend-mode shader creation failed: %s\n", SDL_GetError());
    if (vs != nullptr) SDL_ReleaseGPUShader(device, vs);
    if (fs != nullptr) SDL_ReleaseGPUShader(device, fs);
    g_layer_failed = true;
    return false;
  }

  SDL_GPUColorTargetDescription target;
  std::memset(&target, 0, sizeof(target));
  target.format = g_swapchain_format;
  target.blend_state.enable_blend = false;

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
  g_layer_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
  SDL_ReleaseGPUShader(device, vs);
  SDL_ReleaseGPUShader(device, fs);
  if (g_layer_pipeline == nullptr) {
    fprintf(stderr, "xt_gui: mix-blend-mode pipeline creation failed: %s\n", SDL_GetError());
    g_layer_failed = true;
    return false;
  }

  SDL_GPUSamplerCreateInfo sampler_info;
  std::memset(&sampler_info, 0, sizeof(sampler_info));
  sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
  sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
  sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g_layer_sampler = SDL_CreateGPUSampler(device, &sampler_info);
  if (g_layer_sampler == nullptr) {
    fprintf(stderr, "xt_gui: mix-blend-mode sampler creation failed: %s\n", SDL_GetError());
    SDL_ReleaseGPUGraphicsPipeline(device, g_layer_pipeline);
    g_layer_pipeline = nullptr;
    g_layer_failed = true;
    return false;
  }
  g_layer_format = g_swapchain_format;
  return true;
}

void releaseSceneTextures(SDL_GPUDevice *device, XtGuiGeometry *geometry) {
  for (SDL_GPUTexture *texture : geometry->scene_textures) {
    if (texture != nullptr) SDL_ReleaseGPUTexture(device, texture);
  }
  geometry->scene_textures.clear();
  geometry->scene_width = 0;
  geometry->scene_height = 0;
  geometry->scene_format = SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool prepareSceneTextures(SDL_GPUDevice *device, XtGuiGeometry *geometry, int pairs, int width,
                          int height, SDL_GPUTextureFormat format) {
  if (geometry->scene_format != format || geometry->scene_width != (uint32_t)width ||
      geometry->scene_height != (uint32_t)height) {
    releaseSceneTextures(device, geometry);
  }
  if (geometry->scene_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    geometry->scene_format = format;
    geometry->scene_width = (uint32_t)width;
    geometry->scene_height = (uint32_t)height;
  }
  while ((int)geometry->scene_textures.size() < pairs * 2) {
    SDL_GPUTextureCreateInfo info;
    std::memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = (Uint32)width;
    info.height = (Uint32)height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
    if (texture == nullptr) {
      fprintf(stderr, "xt_gui: mix-blend-mode texture creation failed: %s\n", SDL_GetError());
      releaseSceneTextures(device, geometry);
      g_layer_failed = true;
      return false;
    }
    geometry->scene_textures.push_back(texture);
  }
  return true;
}

SDL_GPUTexture *sceneTexture(XtGuiGeometry *geometry, int depth, int parity) {
  size_t index = (size_t)depth * 2 + (size_t)(parity & 1);
  if (index >= geometry->scene_textures.size()) return nullptr;
  return geometry->scene_textures[index];
}

struct SceneContext {
  SDL_GPUCommandBuffer *cmd = nullptr;
  XtGuiGeometry *shapes = nullptr;
  XtGuiGeometry *texts = nullptr;
  XtGuiGeometry *images = nullptr;
  const std::vector<XtGuiDrawRun> *runs = nullptr;
  const DisplayList *list = nullptr;
  int target_width = 0;
  int target_height = 0;
  float pixel_density = 1.0f;
  int max_depth = 0;
  std::vector<int> parity;
};

size_t matchingLayerEnd(const std::vector<XtGuiDrawRun> &runs, size_t begin, size_t end) {
  int depth = 1;
  for (size_t i = begin + 1; i < end; i++) {
    if (runs[i].pipeline == XtGuiDrawRun::LayerBegin) depth++;
    else if (runs[i].pipeline == XtGuiDrawRun::LayerEnd) {
      depth--;
      if (depth == 0) return i;
    }
  }
  return end;
}

void beginPass(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *texture, bool clear,
               const float color[4], SDL_GPURenderPass **pass) {
  SDL_GPUColorTargetInfo target;
  std::memset(&target, 0, sizeof(target));
  target.texture = texture;
  target.clear_color.r = color != nullptr ? color[0] : 0.0f;
  target.clear_color.g = color != nullptr ? color[1] : 0.0f;
  target.clear_color.b = color != nullptr ? color[2] : 0.0f;
  target.clear_color.a = color != nullptr ? color[3] : 0.0f;
  target.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  target.store_op = SDL_GPU_STOREOP_STORE;
  *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
}

void compositePass(SceneContext &context, SDL_GPUTexture *target, SDL_GPUTexture *backdrop,
                   SDL_GPUTexture *layer, int mode) {
  SDL_GPURenderPass *pass = nullptr;
  beginPass(context.cmd, target, true, nullptr, &pass);
  if (pass == nullptr) return;
  SDL_BindGPUGraphicsPipeline(pass, g_layer_pipeline);
  SDL_GPUTextureSamplerBinding bindings[2];
  bindings[0].texture = backdrop;
  bindings[0].sampler = g_layer_sampler;
  bindings[1].texture = layer;
  bindings[1].sampler = g_layer_sampler;
  SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
  pushMode(context.cmd, mode);
  SDL_Rect full;
  full.x = 0;
  full.y = 0;
  full.w = context.target_width;
  full.h = context.target_height;
  SDL_SetGPUScissor(pass, &full);
  SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
  SDL_EndGPURenderPass(pass);
  xtgui::g_perf_frame.drawCalls += 1;
}

void renderSequence(SceneContext &context, size_t begin, size_t end, int depth, bool clear,
                    const float color[4]) {
  const std::vector<XtGuiDrawRun> &runs = *context.runs;
  SDL_GPUTexture *texture = sceneTexture(context.shapes, depth, context.parity[depth]);
  if (texture == nullptr) return;
  SDL_GPURenderPass *pass = nullptr;
  beginPass(context.cmd, texture, clear, color, &pass);
  if (pass == nullptr) return;

  for (size_t i = begin; i < end; i++) {
    const XtGuiDrawRun &run = runs[i];
    if (run.pipeline == XtGuiDrawRun::LayerBegin) {
      SDL_EndGPURenderPass(pass);
      size_t layer_end = matchingLayerEnd(runs, i, end);
      if (depth + 1 < (int)context.parity.size()) {
        /* The child renders to its own pair, then is composited over this
         * scene. `depth` is the parent, `depth + 1` the child. */
        renderSequence(context, i + 1, layer_end, depth + 1, true, nullptr);
        SDL_GPUTexture *child = sceneTexture(context.shapes, depth + 1, context.parity[depth + 1]);
        SDL_GPUTexture *backdrop = texture;
        SDL_GPUTexture *target = sceneTexture(context.shapes, depth,
                                              context.parity[depth] ^ 1);
        int mode = 0;
        if (context.list != nullptr && run.layer < context.list->layers.size())
          mode = (int)context.list->layers[run.layer].blend;
        if (child != nullptr && backdrop != nullptr && target != nullptr) {
          compositePass(context, target, backdrop, child, mode);
          context.parity[depth] ^= 1;
        }
      }
      /* Continue painting later siblings over the composited scene. */
      texture = sceneTexture(context.shapes, depth, context.parity[depth]);
      beginPass(context.cmd, texture, false, nullptr, &pass);
      if (pass == nullptr) return;
      i = layer_end;
      continue;
    }
    if (run.pipeline == XtGuiDrawRun::LayerEnd) continue;

    size_t segment_end = i;
    while (segment_end < end && runs[segment_end].pipeline != XtGuiDrawRun::LayerBegin &&
           runs[segment_end].pipeline != XtGuiDrawRun::LayerEnd) {
      segment_end++;
    }
    drawRuns(pass, runs, i, segment_end, context.shapes, context.texts, context.images,
             context.target_width, context.target_height, context.pixel_density);
    i = segment_end - 1;
  }
  SDL_EndGPURenderPass(pass);
}

}  // namespace

bool layerRendererReady(SDL_GPUDevice *device) { return ensureLayerPipeline(device); }

bool drawLayeredScene(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *swapchain,
                      const float clear_color[4], XtGuiGeometry *shapes, XtGuiGeometry *texts,
                      XtGuiGeometry *images, const DisplayList &list, float viewport_width,
                      float viewport_height, float pixel_density) {
  if (cmd == nullptr || swapchain == nullptr || shapes == nullptr || g_layer_pipeline == nullptr)
    return false;
  int target_width = (int)(viewport_width * pixel_density + 0.5f);
  int target_height = (int)(viewport_height * pixel_density + 0.5f);
  if (target_width <= 0 || target_height <= 0) return false;

  int max_depth = 0;
  int depth = 0;
  for (const XtGuiDrawRun &run : shapes->runs) {
    if (run.pipeline == XtGuiDrawRun::LayerBegin) {
      depth++;
      if (depth > max_depth) max_depth = depth;
    } else if (run.pipeline == XtGuiDrawRun::LayerEnd) {
      if (depth > 0) depth--;
    }
  }
  if (!prepareSceneTextures(g_device, shapes, max_depth + 1, target_width, target_height,
                            g_layer_format))
    return false;

  SceneContext context;
  context.cmd = cmd;
  context.shapes = shapes;
  context.texts = texts;
  context.images = images;
  context.runs = &shapes->runs;
  context.list = &list;
  context.target_width = target_width;
  context.target_height = target_height;
  context.pixel_density = pixel_density;
  context.max_depth = max_depth;
  context.parity.assign((size_t)max_depth + 1, 0);

  static const float kTransparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  renderSequence(context, 0, shapes->runs.size(), 0, true,
                 clear_color != nullptr ? clear_color : kTransparent);

  SDL_GPUTexture *scene = sceneTexture(shapes, 0, context.parity[0]);
  if (scene == nullptr) return true;
  /* Copy the finished scene to the swapchain. Mode -1 samples only the
   * backdrop, so the same texture is bound twice. */
  SDL_GPURenderPass *pass = nullptr;
  beginPass(cmd, swapchain, true, clear_color, &pass);
  if (pass == nullptr) return true;
  SDL_BindGPUGraphicsPipeline(pass, g_layer_pipeline);
  SDL_GPUTextureSamplerBinding bindings[2];
  bindings[0].texture = scene;
  bindings[0].sampler = g_layer_sampler;
  bindings[1].texture = scene;
  bindings[1].sampler = g_layer_sampler;
  SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
  pushMode(cmd, -1);
  SDL_Rect full;
  full.x = 0;
  full.y = 0;
  full.w = target_width;
  full.h = target_height;
  SDL_SetGPUScissor(pass, &full);
  SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
  SDL_EndGPURenderPass(pass);
  xtgui::g_perf_frame.drawCalls += 1;
  return true;
}

void layerRendererDestroy(SDL_GPUDevice *device) {
  if (g_layer_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_layer_pipeline);
    g_layer_pipeline = nullptr;
  }
  if (g_layer_sampler != nullptr) {
    SDL_ReleaseGPUSampler(device, g_layer_sampler);
    g_layer_sampler = nullptr;
  }
  g_layer_format = SDL_GPU_TEXTUREFORMAT_INVALID;
  g_layer_failed = false;
}

}  // namespace renderer_detail
}  // namespace xtgui
