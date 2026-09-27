/*
 * xbintsc GUI engine — SDL_GPU 2D renderer implementation.
 */

#include "renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

#include "paint.h"

namespace {

/* Rounded-rectangle fill. Vertex attributes (matching SDL_GPUVertexAttribute):
 *   0 -> position (pixels, viewport-relative)
 *   1 -> local   (pixels, relative to the rectangle centre)
 *   2 -> half    (rectangle half extents, pixels)
 *   3 -> radius  (pixels)
 *   4 -> colour
 * The push constant is the viewport size in logical pixels. */
const char *const kFillMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FillIn {
    float2 position [[attribute(0)]];
    float2 local    [[attribute(1)]];
    float2 halfsize [[attribute(2)]];
    float  radius   [[attribute(3)]];
    float4 color    [[attribute(4)]];
};

struct FillOut {
    float4 position [[position]];
    float2 local;
    float2 halfsize;
    float  radius;
    float4 color;
};

vertex FillOut vs_main(FillIn in [[stage_in]],
                       constant float2 &viewport [[buffer(0)]]) {
    FillOut out;
    float2 ndc = float2(in.position.x / viewport.x * 2.0 - 1.0,
                        1.0 - in.position.y / viewport.y * 2.0);
    out.position = float4(ndc, 0.0, 1.0);
    out.local = in.local;
    out.halfsize = in.halfsize;
    out.radius = in.radius;
    out.color = in.color;
    return out;
}

fragment float4 fs_main(FillOut in [[stage_in]]) {
    float2 d = abs(in.local) - in.halfsize + in.radius;
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - in.radius;
    float aa = max(fwidth(dist), 0.0001);
    float alpha = clamp(0.5 - dist / aa, 0.0, 1.0);
    return float4(in.color.rgb, in.color.a * alpha);
}
)MSL";

struct Vertex {
  float x, y;
  float lx, ly;
  float hx, hy;
  float radius;
  float r, g, b, a;
};

SDL_GPUGraphicsPipeline *g_pipeline = nullptr;
int g_renderer_failed = 0;

void pushRect(std::vector<Vertex> &verts, const xtgui::PaintRect &rect) {
  float hw = rect.width * 0.5f;
  float hh = rect.height * 0.5f;
  float cx = rect.x + hw;
  float cy = rect.y + hh;
  float radius = std::min(rect.radius, std::min(hw, hh));

  const float local_x[4] = {-hw, hw, hw, -hw};
  const float local_y[4] = {-hh, -hh, hh, hh};
  Vertex corners[4];
  for (int i = 0; i < 4; i++) {
    corners[i].x = cx + local_x[i];
    corners[i].y = cy + local_y[i];
    corners[i].lx = local_x[i];
    corners[i].ly = local_y[i];
    corners[i].hx = hw;
    corners[i].hy = hh;
    corners[i].radius = radius;
    corners[i].r = rect.color.r;
    corners[i].g = rect.color.g;
    corners[i].b = rect.color.b;
    corners[i].a = rect.color.a;
  }
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; i++) verts.push_back(corners[order[i]]);
}

}  // namespace

bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference) {
  if (g_pipeline != nullptr) return true;
  if (g_renderer_failed) return false;
  if ((SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) == 0) {
    fprintf(stderr,
            "xt_gui: backend has no MSL shader support; shape painting disabled "
            "(Vulkan/D3D12 shader blobs are a TODO)\n");
    g_renderer_failed = 1;
    return false;
  }

  SDL_GPUShaderCreateInfo vs_info;
  memset(&vs_info, 0, sizeof(vs_info));
  vs_info.code_size = strlen(kFillMSL);
  vs_info.code = (const Uint8 *)kFillMSL;
  vs_info.entrypoint = "vs_main";
  vs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vs_info.num_uniform_buffers = 1;
  SDL_GPUShader *vs = SDL_CreateGPUShader(device, &vs_info);

  SDL_GPUShaderCreateInfo fs_info;
  memset(&fs_info, 0, sizeof(fs_info));
  fs_info.code_size = strlen(kFillMSL);
  fs_info.code = (const Uint8 *)kFillMSL;
  fs_info.entrypoint = "fs_main";
  fs_info.format = SDL_GPU_SHADERFORMAT_MSL;
  fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  SDL_GPUShader *fs = SDL_CreateGPUShader(device, &fs_info);

  if (vs == nullptr || fs == nullptr) {
    fprintf(stderr, "xt_gui: shader creation failed: %s\n", SDL_GetError());
    if (vs != nullptr) SDL_ReleaseGPUShader(device, vs);
    if (fs != nullptr) SDL_ReleaseGPUShader(device, fs);
    g_renderer_failed = 1;
    return false;
  }

  SDL_GPUVertexBufferDescription vb_desc;
  memset(&vb_desc, 0, sizeof(vb_desc));
  vb_desc.slot = 0;
  vb_desc.pitch = sizeof(Vertex);
  vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

  SDL_GPUVertexAttribute attrs[5];
  memset(attrs, 0, sizeof(attrs));
  attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, x)};
  attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, lx)};
  attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(Vertex, hx)};
  attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, (Uint32)offsetof(Vertex, radius)};
  attrs[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, (Uint32)offsetof(Vertex, r)};

  SDL_GPUColorTargetDescription color_target;
  memset(&color_target, 0, sizeof(color_target));
  color_target.format = SDL_GetGPUSwapchainTextureFormat(device, reference);
  color_target.blend_state.enable_blend = true;
  color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  SDL_GPUGraphicsPipelineCreateInfo info;
  memset(&info, 0, sizeof(info));
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
  info.vertex_input_state.num_vertex_buffers = 1;
  info.vertex_input_state.vertex_attributes = attrs;
  info.vertex_input_state.num_vertex_attributes = 5;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  info.target_info.color_target_descriptions = &color_target;
  info.target_info.num_color_targets = 1;

  g_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
  SDL_ReleaseGPUShader(device, vs);
  SDL_ReleaseGPUShader(device, fs);
  if (g_pipeline == nullptr) {
    fprintf(stderr, "xt_gui: pipeline creation failed: %s\n", SDL_GetError());
    g_renderer_failed = 1;
    return false;
  }
  return true;
}

void xt_gui_renderer_destroy(SDL_GPUDevice *device) {
  if (g_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline(device, g_pipeline);
    g_pipeline = nullptr;
  }
  g_renderer_failed = 0;
}

void xt_gui_geometry_destroy(SDL_GPUDevice *device, XtGuiGeometry *geometry) {
  if (geometry->buffer != nullptr) {
    SDL_ReleaseGPUBuffer(device, geometry->buffer);
    geometry->buffer = nullptr;
  }
  geometry->capacity_bytes = 0;
  geometry->vertex_count = 0;
  geometry->dirty = true;
}

void xt_gui_geometry_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                            const xtgui::DisplayList &list) {
  std::vector<Vertex> verts;
  verts.reserve(list.rects.size() * 6);
  for (const xtgui::PaintRect &rect : list.rects) pushRect(verts, rect);
  geometry->vertex_count = (uint32_t)verts.size();
  if (verts.empty()) return;

  uint32_t bytes = (uint32_t)(verts.size() * sizeof(Vertex));
  if (geometry->buffer == nullptr || geometry->capacity_bytes < bytes) {
    if (geometry->buffer != nullptr) SDL_ReleaseGPUBuffer(device, geometry->buffer);
    SDL_GPUBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    geometry->buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    geometry->capacity_bytes = bytes;
  }
  if (geometry->buffer == nullptr) {
    fprintf(stderr, "xt_gui: vertex buffer creation failed: %s\n", SDL_GetError());
    geometry->vertex_count = 0;
    return;
  }

  SDL_GPUTransferBufferCreateInfo transfer_info;
  memset(&transfer_info, 0, sizeof(transfer_info));
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = bytes;
  SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (transfer == nullptr) {
    fprintf(stderr, "xt_gui: transfer buffer creation failed: %s\n", SDL_GetError());
    geometry->vertex_count = 0;
    return;
  }
  void *mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  memcpy(mapped, verts.data(), bytes);
  SDL_UnmapGPUTransferBuffer(device, transfer);

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
  if (cmd == nullptr) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    geometry->vertex_count = 0;
    return;
  }
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTransferBufferLocation source;
  source.transfer_buffer = transfer;
  source.offset = 0;
  SDL_GPUBufferRegion destination;
  destination.buffer = geometry->buffer;
  destination.offset = 0;
  destination.size = bytes;
  SDL_UploadToGPUBuffer(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
}

void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                          XtGuiGeometry *geometry, float viewport_width, float viewport_height) {
  if (g_pipeline == nullptr || geometry->buffer == nullptr || geometry->vertex_count == 0) return;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) return;
  SDL_BindGPUGraphicsPipeline(pass, g_pipeline);
  SDL_GPUBufferBinding binding;
  binding.buffer = geometry->buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
  float viewport[2] = {viewport_width, viewport_height};
  SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
  SDL_DrawGPUPrimitives(pass, geometry->vertex_count, 1, 0, 0);
}
