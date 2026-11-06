/*
 * xbintsc GUI engine — `<canvas>` WebGPU contexts and their backbuffer.
 *
 * A context owns one SDL_GPU texture that both the WebGPU render pass and the
 * page compositor use: the paint pipeline samples the same texture through the
 * `XtWebgpuApi::color_texture` bridge, so no pixels pass through the CPU.
 */

#include <unordered_map>

#include "gui_internal.h"
#include "webgpu_bindings.h"
#include "webgpu.h"

namespace xtgui {
namespace webgpu {
namespace {

std::unordered_map<uint64_t, uint32_t> g_by_canvas;  // canvas id -> context id
std::unordered_map<XtGuiWindow *,
                   std::unordered_map<const xtgui::Node *, uint32_t>>
    g_contexts;
uint64_t g_next_canvas = 1;

uint64_t next_canvas_id() { return (2ULL << 32) | g_next_canvas++; }

bool canvas_node(const Node *node) {
  return node != nullptr && node->isElement() && node->isTag("canvas");
}

uint32_t context_id_of(xt_value self) {
  xt_value value = xt_object_get_cstr(self, "__xt_webgpu_id");
  return XT_IS_NUMBER(value) ? (uint32_t)xt_to_number(value) : 0;
}

SDL_GPUTexture *create_backbuffer(CanvasContext &context) {
  SDL_GPUTextureCreateInfo info = {};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = context.format;
  info.usage = Convert::texture_usage(context.usage, context.format) |
               SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  info.width = context.width > 0 ? context.width : 1;
  info.height = context.height > 0 ? context.height : 1;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  return SDL_CreateGPUTexture(gpu_device(), &info);
}

/* Drop the old backbuffer texture object before creating a new one. */
void release_backbuffer(CanvasContext &context) {
  if (context.texture == 0) return;
  std::shared_ptr<Object> object = get_object(context.texture, Kind::Texture);
  if (object != nullptr) {
    Texture *texture = static_cast<Texture *>(object.get());
    if (texture->gpu != nullptr) SDL_ReleaseGPUTexture(gpu_device(), texture->gpu);
    texture->gpu = nullptr;
    texture->destroyed = true;
  }
  remove_object(context.texture);
  context.texture = 0;
}

uint32_t ensure_backbuffer(CanvasContext &context) {
  if (context.texture != 0) {
    std::shared_ptr<Object> existing = get_object(context.texture, Kind::Texture);
    if (existing != nullptr) return context.texture;
  }
  SDL_GPUTexture *gpu = create_backbuffer(context);
  if (gpu == nullptr) return 0;
  auto texture = std::make_shared<Texture>();
  texture->kind = Kind::Texture;
  texture->device_id = context.device;
  texture->gpu = gpu;
  texture->format = context.format;
  texture->width = context.width;
  texture->height = context.height;
  texture->depth = 1;
  texture->mips = 1;
  texture->samples = 1;
  texture->usage = context.usage;
  texture->dimension = "2d";
  texture->canvas_id = context.canvas_id;
  texture->external = true;
  context.texture = add_object(texture);
  return context.texture;
}

/* -- context methods ------------------------------------------------------ */

xt_value context_configure(Object &object, int argc, xt_value *argv) {
  CanvasContext &context = static_cast<CanvasContext &>(object);
  xt_value descriptor = arg_object(argc, argv, 0);
  if (!XT_IS_OBJECT(descriptor)) {
    throw_type_error("configure needs a descriptor");
    return XT_UNDEFINED;
  }
  const uint32_t device_id = (uint32_t)xt_to_number(
      xt_object_get_cstr(prop_object(descriptor, "device"), "__xt_webgpu_id"));
  std::shared_ptr<Object> device = get_object(device_id, Kind::Device);
  if (device == nullptr) {
    throw_type_error("configure: device is not a GPUDevice");
    return XT_UNDEFINED;
  }
  const std::string format = prop_string(descriptor, "format");
  SDL_GPUTextureFormat sdl_format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  if (!format.empty() && !Convert::texture_format(format, &sdl_format)) {
    return validation(static_cast<Device &>(*device), "configure: unsupported format '" + format + "'");
  }
  if (context.texture != 0 &&
      (context.format != sdl_format || context.width == 0 || context.height == 0)) {
    release_backbuffer(context);
  }
  context.device = device_id;
  context.format = sdl_format;
  context.format_name = format.empty() ? "bgra8unorm" : format;
  context.usage = (uint32_t)prop_u64(descriptor, "usage", 16);
  context.alpha_mode = prop_string(descriptor, "alphaMode");
  if (context.alpha_mode.empty()) context.alpha_mode = "opaque";
  context.configured = true;
  release_backbuffer(context);
  if (ensure_backbuffer(context) == 0) {
    return validation(static_cast<Device &>(*device), "configure: the backbuffer could not be created");
  }
  return XT_UNDEFINED;
}

xt_value context_unconfigure(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CanvasContext &context = static_cast<CanvasContext &>(object);
  release_backbuffer(context);
  context.configured = false;
  return XT_UNDEFINED;
}

xt_value context_get_current_texture(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CanvasContext &context = static_cast<CanvasContext &>(object);
  if (!context.configured) {
    std::shared_ptr<Device> device = device_for(object, nullptr);
    if (device != nullptr) {
      return validation(*device, "getCurrentTexture: the canvas context is not configured");
    }
    return XT_UNDEFINED;
  }
  const uint32_t texture = ensure_backbuffer(context);
  if (texture == 0) return XT_UNDEFINED;
  return handle_of(texture);
}

xt_value context_get_configuration(Object &object, int argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  CanvasContext &context = static_cast<CanvasContext &>(object);
  if (!context.configured) return XT_UNDEFINED;
  xt_value result = xt_object_new();
  xt_object_set(result, xt_string_from_cstr("device"), device_object(context.device));
  xt_object_set(result, xt_string_from_cstr("format"),
                xt_string_from_cstr(context.format_name.c_str()));
  xt_object_set(result, xt_string_from_cstr("usage"), xt_number((double)context.usage));
  xt_object_set(result, xt_string_from_cstr("alphaMode"),
                xt_string_from_cstr(context.alpha_mode.c_str()));
  return result;
}

xt_value context_canvas(Object &object) {
  CanvasContext &context = static_cast<CanvasContext &>(object);
  return context.canvas_js == XT_UNDEFINED ? XT_NULL : context.canvas_js;
}

xt_value context_label(Object &object) { return xt_string_from_cstr(object.label.c_str()); }

}  // namespace

/* -- feature entry points ------------------------------------------------- */

bool canvas_context(XtGuiWindow *win, const void *node_ptr, uint32_t *out_id) {
  const Node *node = static_cast<const Node *>(node_ptr);
  if (win == nullptr || !canvas_node(node)) return false;
  auto found = g_contexts.find(win);
  if (found != g_contexts.end()) {
    auto entry = found->second.find(node);
    if (entry != found->second.end()) {
      *out_id = entry->second;
      return true;
    }
  }
  float width = 300.0f;
  float height = 150.0f;
  if (const std::string *attribute = node->attr("width")) xtgui::xt_css_parse_number(*attribute, &width);
  if (const std::string *attribute = node->attr("height")) xtgui::xt_css_parse_number(*attribute, &height);
  auto context = std::make_shared<CanvasContext>();
  context->kind = Kind::CanvasContext;
  context->device_id = 0;
  context->canvas_id = next_canvas_id();
  context->width = (uint32_t)(width + 0.5f);
  context->height = (uint32_t)(height + 0.5f);
  const uint32_t id = add_object(context);
  g_contexts[win][node] = id;
  g_by_canvas[context->canvas_id] = id;
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, width, height, context->canvas_id);
  }
  *out_id = id;
  return true;
}

bool canvas_find(XtGuiWindow *win, const void *node_ptr, uint32_t *out_id) {
  const Node *node = static_cast<const Node *>(node_ptr);
  auto found = g_contexts.find(win);
  if (found == g_contexts.end()) return false;
  auto entry = found->second.find(node);
  if (entry == found->second.end()) return false;
  *out_id = entry->second;
  return true;
}

void canvas_forget(XtGuiWindow *win, const void *node_ptr) {
  const Node *node = static_cast<const Node *>(node_ptr);
  auto found = g_contexts.find(win);
  if (found == g_contexts.end()) return;
  auto entry = found->second.find(node);
  if (entry == found->second.end()) return;
  std::shared_ptr<Object> object = get_object(entry->second, Kind::CanvasContext);
  if (object != nullptr) {
    CanvasContext *context = static_cast<CanvasContext *>(object.get());
    g_by_canvas.erase(context->canvas_id);
    release_backbuffer(*context);
    remove_object(context->id);
  }
  found->second.erase(entry);
}

void canvas_shutdown(XtGuiWindow *win) {
  auto found = g_contexts.find(win);
  if (found == g_contexts.end()) return;
  for (const auto &entry : found->second) {
    std::shared_ptr<Object> object = get_object(entry.second, Kind::CanvasContext);
    if (object == nullptr) continue;
    CanvasContext *context = static_cast<CanvasContext *>(object.get());
    g_by_canvas.erase(context->canvas_id);
    release_backbuffer(*context);
    remove_object(context->id);
  }
  g_contexts.erase(win);
}

void canvas_resize(uint32_t id, int width, int height) {
  std::shared_ptr<Object> object = get_object(id, Kind::CanvasContext);
  if (object == nullptr) return;
  CanvasContext *context = static_cast<CanvasContext *>(object.get());
  if ((uint32_t)width == context->width && (uint32_t)height == context->height) return;
  context->width = (uint32_t)width;
  context->height = (uint32_t)height;
  if (context->configured) {
    release_backbuffer(*context);
    ensure_backbuffer(*context);
  }
}

void *canvas_color_texture(uint64_t canvas_id) {
  auto found = g_by_canvas.find(canvas_id);
  if (found == g_by_canvas.end()) return nullptr;
  std::shared_ptr<Object> object = get_object(found->second, Kind::CanvasContext);
  if (object == nullptr) return nullptr;
  CanvasContext *context = static_cast<CanvasContext *>(object.get());
  if (context->texture == 0) return nullptr;
  std::shared_ptr<Object> texture = get_object(context->texture, Kind::Texture);
  return texture == nullptr ? nullptr : static_cast<Texture *>(texture.get())->gpu;
}

bool canvas_info(uint32_t id, float *width, float *height, uint64_t *canvas_id) {
  std::shared_ptr<Object> object = get_object(id, Kind::CanvasContext);
  if (object == nullptr) return false;
  CanvasContext *context = static_cast<CanvasContext *>(object.get());
  *width = (float)context->width;
  *height = (float)context->height;
  *canvas_id = context->canvas_id;
  return true;
}

xt_value context_proto() { return proto_for(Kind::CanvasContext); }

xt_value context_handle(xt_value self, uint32_t id) {
  std::shared_ptr<Object> object = get_object(id, Kind::CanvasContext);
  if (object == nullptr) return XT_NULL;
  CanvasContext *context = static_cast<CanvasContext *>(object.get());
  context->canvas_js = self;
  xt_gc_add_root(&context->canvas_js);
  return wrap_object(object, context_proto());
}

void register_canvas_methods() {
  add_getter(Kind::CanvasContext, "canvas", context_canvas);
  add_getter(Kind::CanvasContext, "label", context_label);
  add_method(Kind::CanvasContext, "configure", context_configure);
  add_method(Kind::CanvasContext, "unconfigure", context_unconfigure);
  add_method(Kind::CanvasContext, "getCurrentTexture", context_get_current_texture);
  add_method(Kind::CanvasContext, "getConfiguration", context_get_configuration);
}

}  // namespace webgpu
}  // namespace xtgui
