/*
 * xbintsc GUI engine — WebGPU object model.
 *
 * The engine's GPU device (SDL_GPU, shared with the HTML compositor) backs the
 * `navigator.gpu` surface: adapters, devices, queues, buffers, textures,
 * samplers, shader modules, bind groups, pipelines, command encoders and the
 * `<canvas>` context. Objects live in one registry keyed by an integer id; the
 * JavaScript handle only carries that id, so a handle can be validated against
 * its kind before any method runs.
 */
#ifndef XT_GUI_WEBGPU_H
#define XT_GUI_WEBGPU_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gui_engine.h"
#include "wgsl.h"

namespace xtgui {
namespace webgpu {

enum class Kind : int {
  Gpu = 1,
  Adapter,
  Device,
  Queue,
  Buffer,
  Texture,
  TextureView,
  Sampler,
  ShaderModule,
  BindGroupLayout,
  PipelineLayout,
  BindGroup,
  RenderPipeline,
  ComputePipeline,
  CommandEncoder,
  CommandBuffer,
  RenderPassEncoder,
  ComputePassEncoder,
  RenderBundleEncoder,
  RenderBundle,
  QuerySet,
  CanvasContext
};

struct Object {
  Kind kind = Kind::Gpu;
  uint32_t id = 0;
  uint32_t device_id = 0;
  std::string label;
  virtual ~Object() = default;
};

/** `pushErrorScope` / `popErrorScope` bookkeeping. */
struct ErrorScope {
  int filter = 0;
  bool filled = false;
  int kind = 0;
  std::string message;
};

struct Device : Object {
  std::vector<ErrorScope> scopes;
  bool destroyed = false;
  bool lost = false;
  /** The JS device, rooted so uncaptured-error listeners stay reachable. */
  xt_value js = XT_UNDEFINED;
  /** `device.lost`: created with the device, settled by `destroy()`. */
  xt_value lost_promise = XT_UNDEFINED;
  xt_value lost_resolve = XT_UNDEFINED;
  std::string lost_reason;
  /** Lazily created resources used when a binding is missing. */
  SDL_GPUTexture *fallback_texture = nullptr;
  SDL_GPUSampler *fallback_sampler = nullptr;
};

struct Buffer : Object {
  uint64_t size = 0;
  uint32_t usage = 0;
  SDL_GPUBuffer *gpu = nullptr;
  bool has_gpu = false;
  /** CPU mirror for buffers used as uniform buffers (SDL pushes them by value). */
  bool has_shadow = false;
  std::vector<uint8_t> shadow;
  int map_state = 0;  // 0 unmapped, 1 pending, 2 mapped
  int map_mode = 0;   // 1 READ, 2 WRITE
  bool destroyed = false;
  std::vector<uint8_t> mapped;
  xt_value mapped_array = XT_UNDEFINED;
  uint64_t mapped_offset = 0;
  uint64_t mapped_size = 0;
};

struct Texture : Object {
  SDL_GPUTexture *gpu = nullptr;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 1;
  uint32_t mips = 1;
  uint32_t samples = 1;
  uint32_t usage = 0;
  std::string dimension = "2d";
  /** Non-zero for a `<canvas>` backbuffer: the compositor samples this. */
  uint64_t canvas_id = 0;
  bool external = false;
  bool destroyed = false;
};

struct TextureView : Object {
  uint32_t texture = 0;
};

struct Sampler : Object {
  SDL_GPUSampler *gpu = nullptr;
  bool compare = false;
};

struct ShaderModule : Object {
  std::string source;
  bool valid = true;
  std::string error;
  int error_line = 0;
  int error_column = 0;
  /** Translations keyed by `<stage>:<entry>`; the GLSL and binding layout are
   * produced once per entry point. */
  std::map<std::string, wgsl::StageCode> stages;
};

struct LayoutEntry {
  int binding = 0;
  int visibility = 0;  // 1 vertex, 2 fragment, 4 compute
  std::string type;    // buffer, texture, sampler, storage-texture
  std::string buffer_type;  // uniform, storage, read-only-storage
  bool has_dynamic_offset = false;
  uint64_t min_binding_size = 0;
};

struct BindGroupLayout : Object {
  std::vector<LayoutEntry> entries;
};

struct PipelineLayout : Object {
  std::vector<uint32_t> layouts;
};

struct BindGroupEntry {
  int binding = 0;
  uint32_t buffer = 0;
  uint32_t sampler = 0;
  uint32_t view = 0;
  uint64_t offset = 0;
  uint64_t size = 0;
};

struct BindGroup : Object {
  uint32_t layout = 0;
  std::vector<BindGroupEntry> entries;

  const BindGroupEntry *find(int binding) const {
    for (const BindGroupEntry &entry : entries) {
      if (entry.binding == binding) return &entry;
    }
    return nullptr;
  }
};

/** One compiled SDL shader stage plus the binding layout of its GLSL. */
struct StageResources {
  wgsl::StageCode code;
  SDL_GPUShader *shader = nullptr;
  /** SPIR-V words and, on Metal, the MSL text the device consumes. */
  std::vector<uint32_t> spirv;
  std::string msl;
  SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
  std::string entry = "main";
};

struct RenderPipeline : Object {
  SDL_GPUGraphicsPipeline *gpu = nullptr;
  uint32_t layout = 0;
  StageResources vertex;
  StageResources fragment;
  bool has_fragment = false;
  bool valid = true;
};

struct ComputePipeline : Object {
  SDL_GPUComputePipeline *gpu = nullptr;
  uint32_t layout = 0;
  StageResources stage;
  uint32_t workgroup[3] = {1, 1, 1};
  bool valid = true;
};

struct QuerySet : Object {
  int type = 0;  // 0 occlusion, 1 timestamp
  uint32_t count = 0;
};

struct CommandEncoder : Object {
  SDL_GPUCommandBuffer *cmd = nullptr;
  bool finished = false;
};

struct CommandBuffer : Object {
  SDL_GPUCommandBuffer *cmd = nullptr;
};

struct BindGroupSlot {
  uint32_t group = 0;
  std::vector<uint32_t> dynamic_offsets;
};

struct RenderPassEncoder : Object {
  uint32_t encoder = 0;
  SDL_GPURenderPass *pass = nullptr;
  bool ended = false;
  uint32_t pipeline = 0;
  std::map<int, BindGroupSlot> groups;
  uint32_t vertex_buffers[16] = {0};
  uint64_t vertex_offsets[16] = {0};
  uint32_t index_buffer = 0;
  uint64_t index_offset = 0;
  int index_format = 0;  // 16 or 32
};

struct ComputePassEncoder : Object {
  uint32_t encoder = 0;
  SDL_GPUComputePass *pass = nullptr;
  bool begun = false;
  bool ended = false;
  uint32_t pipeline = 0;
  std::map<int, BindGroupSlot> groups;
};

struct RenderBundleEncoder : Object {
  struct Command {
    int kind = 0;  // 1 pipeline, 2 group, 3 vertex, 4 index, 5 draw, 6 draw-indexed
    uint32_t object = 0;
    int slot = 0;
    uint64_t offset = 0;
    uint32_t count = 0;
    uint32_t instances = 0;
    uint32_t first = 0;
    std::vector<uint32_t> dynamic_offsets;
  };
  std::vector<Command> commands;
  bool finished = false;
};

struct RenderBundle : Object {
  std::vector<RenderBundleEncoder::Command> commands;
};

struct CanvasContext : Object {
  uint64_t canvas_id = 0;
  uint32_t device = 0;
  uint32_t texture = 0;
  uint32_t width = 300;
  uint32_t height = 150;
  bool configured = false;
  SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  std::string format_name = "bgra8unorm";
  uint32_t usage = 0;
  std::string alpha_mode = "opaque";
  xt_value canvas_js = XT_UNDEFINED;
};

/* -- object registry (webgpu_objects.cpp) --------------------------------- */

uint32_t add_object(std::shared_ptr<Object> object);
std::shared_ptr<Object> get_object(uint32_t id, Kind kind);
std::shared_ptr<Object> get_object(uint32_t id);
void remove_object(uint32_t id);
void device_children(uint32_t device_id, std::vector<uint32_t> *out);

/* -- errors and queue state ----------------------------------------------- */

enum class ErrorKind { Validation = 0, OutOfMemory = 1, Internal = 2 };
void record_error(Device &device, ErrorKind kind, const std::string &message);
xt_value make_error(ErrorKind kind, const std::string &message);
xt_value make_device_lost(const std::string &reason);

/** The single SDL_GPU device the GUI and WebGPU share, or null. */
SDL_GPUDevice *gpu_device();

/* -- feature entry points (webgpu_feature.cpp) ---------------------------- */

xt_value navigator_gpu();
xt_value host_type(const std::string &name);
xt_value context_proto();
xt_value context_handle(xt_value self, uint32_t id);
bool canvas_context(XtGuiWindow *win, const void *node, uint32_t *out_id);
bool canvas_find(XtGuiWindow *win, const void *node, uint32_t *out_id);
void canvas_forget(XtGuiWindow *win, const void *node);
void canvas_shutdown(XtGuiWindow *win);
void canvas_resize(uint32_t id, int width, int height);
void *canvas_color_texture(uint64_t canvas_id);
bool canvas_info(uint32_t id, float *width, float *height, uint64_t *canvas_id);

/* -- conversion helpers (webgpu_convert.cpp) ------------------------------ */

struct Convert {
  static bool texture_format(const std::string &name, SDL_GPUTextureFormat *out);
  static const char *texture_format_name(SDL_GPUTextureFormat format);
  static bool vertex_format(const std::string &name, SDL_GPUVertexElementFormat *out);
  static SDL_GPUPrimitiveType topology(const std::string &name);
  static SDL_GPUCompareOp compare(const std::string &name);
  static SDL_GPUBlendFactor blend_factor(const std::string &name);
  static SDL_GPUBlendOp blend_op(const std::string &name);
  static SDL_GPUSamplerAddressMode address_mode(const std::string &name);
  static SDL_GPUFilter filter(const std::string &name);
  static SDL_GPUSamplerMipmapMode mipmap_mode(const std::string &name);
  static uint32_t texture_usage(uint32_t usage, SDL_GPUTextureFormat format);
  static uint32_t bytes_per_pixel(SDL_GPUTextureFormat format);
  static bool is_depth_format(SDL_GPUTextureFormat format);
};

/* -- binding descriptors -------------------------------------------------- */

std::string arg_string(int argc, xt_value *argv, int index);
double arg_number(int argc, xt_value *argv, int index);
uint64_t arg_u64(int argc, xt_value *argv, int index);
bool arg_bool(int argc, xt_value *argv, int index, bool fallback);
xt_value arg_object(int argc, xt_value *argv, int index);
std::string prop_string(xt_value object, const char *name);
double prop_number(xt_value object, const char *name, double fallback);
uint64_t prop_u64(xt_value object, const char *name, uint64_t fallback);
bool prop_bool(xt_value object, const char *name, bool fallback);
std::string prop_name(xt_value object, const char *name);
xt_value prop_object(xt_value object, const char *name);
bool read_bytes(xt_value value, std::vector<uint8_t> *out);

/** The JS handle for a native object; `proto` is the interface prototype. */
xt_value wrap_object(const std::shared_ptr<Object> &object, xt_value proto);
xt_value queue_object(uint32_t device_id);
xt_value device_object(uint32_t device_id);

/* -- pipeline/shader helpers (webgpu_shader.cpp) -------------------------- */

bool translate_stage(Device &device, ShaderModule &module, const std::string &entry, int stage,
                     wgsl::StageCode *code, std::string *error);
bool compile_shader(Device &device, ShaderModule &module, const std::string &entry, int stage,
                    StageResources *resources, std::string *error);
/** Compile without creating an SDL shader (compute pipelines take the code). */
bool compile_shader_code(Device &device, ShaderModule &module, const std::string &entry, int stage,
                         StageResources *resources, std::string *error);
/** One implicit bind-group layout per group index used by the stages. */
std::vector<uint32_t> make_auto_layouts(
    Device &device, const std::vector<std::pair<int, wgsl::StageCode>> &stages);
bool build_bind_group_layout(Device &device, xt_value descriptor,
                             const std::shared_ptr<BindGroupLayout> &layout, std::string *error);

/* -- pass helpers shared with bundles ------------------------------------- */

struct PassState {
  RenderPassEncoder *pass = nullptr;
  RenderPipeline *pipeline = nullptr;
};

}  // namespace webgpu
}  // namespace xtgui

#endif /* XT_GUI_WEBGPU_H */
