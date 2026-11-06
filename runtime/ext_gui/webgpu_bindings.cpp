/*
 * xbintsc GUI engine — WebGPU prototype/method dispatch and host types.
 *
 * Every WebGPU interface is a prototype object whose members are closures that
 * capture one index into `g_methods`. The dispatcher validates the receiver's
 * kind, resolves the native object and calls the handler, so a method called
 * on the wrong interface behaves like a browser's "Illegal invocation".
 */

#include "webgpu_bindings.h"

#include <map>
#include <string>
#include <vector>

namespace xtgui {
namespace webgpu {
namespace {

struct Method {
  Kind kind = Kind::Gpu;
  std::string name;
  Handler handler = nullptr;
  Getter getter = nullptr;
};

std::vector<Method> g_methods;
std::map<int, xt_value> g_protos;      // Kind -> prototype
std::map<int, xt_value> g_errors;      // ErrorKind -> prototype
std::map<std::string, xt_value> g_types;
std::map<std::string, xt_value> g_namespaces;
xt_value g_gpu = XT_UNDEFINED;
xt_value g_device_lost = XT_UNDEFINED;
bool g_registered = false;
xt_value g_plain_proto = XT_UNDEFINED;
xt_value g_plain_ctor = XT_UNDEFINED;

xt_value host_ctor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  throw_type_error("Illegal constructor");
  return XT_UNDEFINED;
}

/** A constructor value whose `.prototype` is `proto` (for `instanceof`). */
xt_value constructor_for(const std::string &name, xt_value proto) {
  xt_value name_value = xt_string_from_cstr(name.c_str());
  xt_value ctor = xt_closure_new((void *)host_ctor, 1, &name_value);
  xt_function_set_metadata(ctor, xt_string_from_cstr(name.c_str()), 0);
  xt_function_set_prototype(ctor, proto);
  xt_object_set(proto, xt_string_from_cstr("constructor"), ctor);
  return ctor;
}

Kind kind_by_name(const std::string &name, bool *found) {
  *found = true;
  if (name == "GPU") return Kind::Gpu;
  if (name == "GPUAdapter") return Kind::Adapter;
  if (name == "GPUDevice") return Kind::Device;
  if (name == "GPUQueue") return Kind::Queue;
  if (name == "GPUBuffer") return Kind::Buffer;
  if (name == "GPUTexture") return Kind::Texture;
  if (name == "GPUTextureView") return Kind::TextureView;
  if (name == "GPUSampler") return Kind::Sampler;
  if (name == "GPUShaderModule") return Kind::ShaderModule;
  if (name == "GPUBindGroupLayout") return Kind::BindGroupLayout;
  if (name == "GPUPipelineLayout") return Kind::PipelineLayout;
  if (name == "GPUBindGroup") return Kind::BindGroup;
  if (name == "GPURenderPipeline") return Kind::RenderPipeline;
  if (name == "GPUComputePipeline") return Kind::ComputePipeline;
  if (name == "GPUCommandEncoder") return Kind::CommandEncoder;
  if (name == "GPURenderPassEncoder") return Kind::RenderPassEncoder;
  if (name == "GPUComputePassEncoder") return Kind::ComputePassEncoder;
  if (name == "GPURenderBundleEncoder") return Kind::RenderBundleEncoder;
  if (name == "GPURenderBundle") return Kind::RenderBundle;
  if (name == "GPUQuerySet") return Kind::QuerySet;
  if (name == "GPUCanvasContext") return Kind::CanvasContext;
  *found = false;
  return Kind::Gpu;
}

xt_value dispatch(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  const int index = (int)xt_to_number(xt_closure_env(env, 0));
  if (index < 0 || (size_t)index >= g_methods.size()) return XT_UNDEFINED;
  const Method &method = g_methods[(size_t)index];
  if (!XT_IS_OBJECT(self)) {
    throw_type_error("Illegal invocation: " + method.name);
    return XT_UNDEFINED;
  }
  xt_value kind_value = xt_object_get_cstr(self, "__xt_webgpu_kind");
  if (!XT_IS_NUMBER(kind_value) || (Kind)xt_to_number(kind_value) != method.kind) {
    throw_type_error("Illegal invocation: " + method.name);
    return XT_UNDEFINED;
  }
  const uint32_t id = (uint32_t)xt_to_number(xt_object_get_cstr(self, "__xt_webgpu_id"));
  std::shared_ptr<Object> object = get_object(id, method.kind);
  if (object == nullptr) {
    throw_type_error("This WebGPU object has been destroyed");
    return XT_UNDEFINED;
  }
  if (method.getter != nullptr) return method.getter(*object);
  return method.handler(*object, argc, argv);
}

void ensure_registered() {
  if (g_registered) return;
  g_registered = true;
  register_gpu_methods();
  register_resource_methods();
  register_buffer_methods();
  register_pipeline_methods();
  register_queue_methods();
  register_command_methods();
  register_render_pass_methods();
  register_compute_pass_methods();
  register_canvas_methods();
}

xt_value namespace_object(const char *name) {
  auto found = g_namespaces.find(name);
  if (found != g_namespaces.end()) return found->second;
  xt_value object = xt_object_new();
  g_namespaces[name] = object;
  xt_gc_add_root(&g_namespaces[name]);
  return object;
}

void set_number_prop(xt_value object, const char *name, double value) {
  xt_object_set(object, xt_string_from_cstr(name), xt_number(value));
}

xt_value gpu_feature_has(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

}  // namespace

void add_method(Kind kind, const char *name, Handler handler) {
  Method method;
  method.kind = kind;
  method.name = name;
  method.handler = handler;
  g_methods.push_back(std::move(method));
}

void add_getter(Kind kind, const char *name, Getter getter) {
  Method method;
  method.kind = kind;
  method.name = name;
  method.getter = getter;
  g_methods.push_back(std::move(method));
}

xt_value proto_for(Kind kind) {
  ensure_registered();
  auto found = g_protos.find((int)kind);
  if (found != g_protos.end()) return found->second;
  xt_value proto = xt_object_new();
  g_protos[(int)kind] = proto;
  xt_gc_add_root(&g_protos[(int)kind]);
  for (size_t i = 0; i < g_methods.size(); i++) {
    const Method &method = g_methods[i];
    if (method.kind != kind) continue;
    xt_value index = xt_number((double)i);
    xt_value closure = xt_closure_new((void *)dispatch, 1, &index);
    if (method.getter != nullptr) {
      xt_object_define_getter(proto, xt_string_from_cstr(method.name.c_str()), closure);
    } else {
      xt_object_set(proto, xt_string_from_cstr(method.name.c_str()), closure);
    }
  }
  return proto;
}

xt_value error_proto(int kind) {
  ensure_registered();
  auto found = g_errors.find(kind);
  if (found != g_errors.end()) return found->second;
  xt_value proto = xt_object_new();
  g_errors[kind] = proto;
  xt_gc_add_root(&g_errors[kind]);
  return proto;
}

xt_value device_lost_proto() {
  if (XT_IS_OBJECT(g_device_lost)) return g_device_lost;
  g_device_lost = xt_object_new();
  xt_gc_add_root(&g_device_lost);
  return g_device_lost;
}

xt_value listener_list(Device &device) {
  if (!XT_IS_OBJECT(device.js)) return xt_array_new(0, nullptr);
  xt_value list = xt_object_get_cstr(device.js, "__xt_webgpu_listeners");
  if (!XT_IS_ARRAY(list)) {
    list = xt_array_new(0, nullptr);
    xt_object_set(device.js, xt_string_from_cstr("__xt_webgpu_listeners"), list);
  }
  return list;
}

void dispatch_uncaptured_error(Device &device, xt_value error) {
  if (!XT_IS_OBJECT(device.js)) return;
  xt_value list = listener_list(device);
  const uint32_t count = (uint32_t)xt_to_number(xt_array_length(list));
  if (count == 0) return;
  xt_value event = xt_object_new();
  xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr("uncapturederror"));
  xt_object_set(event, xt_string_from_cstr("error"), error);
  for (uint32_t i = 0; i < count; i++) {
    xt_value listener = xt_array_get(list, xt_number((double)i));
    if (XT_IS_FUNCTION(listener)) xt_call_with_this(listener, device.js, 1, &event);
  }
}

void throw_type_error(const std::string &message) {
  xt_value text = xt_string_from_cstr(message.c_str());
  xt_throw(xt_error_ctor_by_name(xt_string_from_cstr("TypeError"), 1, &text));
}

xt_value validation(Device &device, const std::string &message, xt_value fallback) {
  record_error(device, ErrorKind::Validation, message);
  return fallback;
}

std::shared_ptr<Device> device_for(const Object &object, std::string *error) {
  std::shared_ptr<Object> found = get_object(object.device_id, Kind::Device);
  if (found == nullptr) {
    if (error != nullptr) *error = "the WebGPU device is no longer available";
    return nullptr;
  }
  return std::static_pointer_cast<Device>(found);
}

xt_value handle_of(uint32_t id) {
  std::shared_ptr<Object> object = get_object(id);
  if (object == nullptr) return XT_UNDEFINED;
  return wrap_object(object, proto_for(object->kind));
}

xt_value resolved(xt_value value) { return xt_promise_resolve(value); }

/* -- host types ----------------------------------------------------------- */

xt_value host_type(const std::string &name) {
  ensure_registered();
  auto cached = g_types.find(name);
  if (cached != g_types.end()) return cached->second;

  if (name == "GPUValidationError") {
    g_types[name] = constructor_for(name, error_proto(0));
    xt_gc_add_root(&g_types[name]);
    return g_types[name];
  }
  if (name == "GPUOutOfMemoryError") {
    g_types[name] = constructor_for(name, error_proto(1));
    xt_gc_add_root(&g_types[name]);
    return g_types[name];
  }
  if (name == "GPUInternalError") {
    g_types[name] = constructor_for(name, error_proto(2));
    xt_gc_add_root(&g_types[name]);
    return g_types[name];
  }
  if (name == "GPUDeviceLostInfo") {
    g_types[name] = constructor_for(name, device_lost_proto());
    xt_gc_add_root(&g_types[name]);
    return g_types[name];
  }

  if (name == "GPUBufferUsage") {
    xt_value ns = namespace_object("GPUBufferUsage");
    const struct { const char *key; double value; } kValues[] = {
        {"MAP_READ", 1},    {"MAP_WRITE", 2}, {"COPY_SRC", 4},  {"COPY_DST", 8},
        {"INDEX", 16},      {"VERTEX", 32},   {"UNIFORM", 64},  {"STORAGE", 128},
        {"INDIRECT", 256},  {"QUERY_RESOLVE", 512}};
    for (const auto &value : kValues) set_number_prop(ns, value.key, value.value);
    g_types[name] = ns;
    return ns;
  }
  if (name == "GPUTextureUsage") {
    xt_value ns = namespace_object("GPUTextureUsage");
    const struct { const char *key; double value; } kValues[] = {
        {"COPY_SRC", 1}, {"COPY_DST", 2}, {"TEXTURE_BINDING", 4},
        {"STORAGE_BINDING", 8}, {"RENDER_ATTACHMENT", 16}};
    for (const auto &value : kValues) set_number_prop(ns, value.key, value.value);
    g_types[name] = ns;
    return ns;
  }
  if (name == "GPUShaderStage") {
    xt_value ns = namespace_object("GPUShaderStage");
    set_number_prop(ns, "VERTEX", 1);
    set_number_prop(ns, "FRAGMENT", 2);
    set_number_prop(ns, "COMPUTE", 4);
    g_types[name] = ns;
    return ns;
  }
  if (name == "GPUMapMode") {
    xt_value ns = namespace_object("GPUMapMode");
    set_number_prop(ns, "READ", 1);
    set_number_prop(ns, "WRITE", 2);
    g_types[name] = ns;
    return ns;
  }
  if (name == "GPUColorWrite") {
    xt_value ns = namespace_object("GPUColorWrite");
    set_number_prop(ns, "RED", 1);
    set_number_prop(ns, "GREEN", 2);
    set_number_prop(ns, "BLUE", 4);
    set_number_prop(ns, "ALPHA", 8);
    set_number_prop(ns, "ALL", 15);
    g_types[name] = ns;
    return ns;
  }

  bool found = false;
  const Kind kind = kind_by_name(name, &found);
  if (!found) {
    /* A class without native state (GPUAdapterInfo, GPUCompilationInfo, …):
     * a constructor over a plain prototype keeps `instanceof` meaningful. */
    if (name.compare(0, 3, "GPU") != 0) return XT_UNDEFINED;
    if (!XT_IS_OBJECT(g_plain_proto)) {
      g_plain_proto = xt_object_new();
      xt_gc_add_root(&g_plain_proto);
      g_plain_ctor = constructor_for("GPUObject", g_plain_proto);
      xt_gc_add_root(&g_plain_ctor);
    }
    xt_value ctor = xt_closure_new((void *)host_ctor, 0, nullptr);
    xt_function_set_prototype(ctor, g_plain_proto);
    g_types[name] = ctor;
    xt_gc_add_root(&g_types[name]);
    return ctor;
  }
  xt_value proto = proto_for(kind);
  g_types[name] = constructor_for(name, proto);
  xt_gc_add_root(&g_types[name]);
  return g_types[name];
}

xt_value navigator_gpu() {
  ensure_registered();
  if (XT_IS_OBJECT(g_gpu)) return g_gpu;
  auto gpu = std::make_shared<Object>();
  gpu->kind = Kind::Gpu;
  const uint32_t id = add_object(gpu);
  g_gpu = wrap_object(gpu, proto_for(Kind::Gpu));
  xt_gc_add_root(&g_gpu);
  xt_value features = xt_object_new();
  xt_value has = xt_closure_new((void *)gpu_feature_has, 0, nullptr);
  xt_object_set(features, xt_string_from_cstr("has"), has);
  xt_object_set(features, xt_string_from_cstr("size"), xt_number(0));
  xt_object_set(g_gpu, xt_string_from_cstr("wgslLanguageFeatures"), features);
  (void)id;
  return g_gpu;
}

}  // namespace webgpu
}  // namespace xtgui
