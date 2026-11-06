/*
 * xbintsc GUI engine — the WebGPU object registry, errors and JS handles.
 */

#include "webgpu.h"

#include <algorithm>

#include "gui_internal.h"
#include "webgpu_bindings.h"

namespace xtgui {
namespace webgpu {
namespace {

std::unordered_map<uint32_t, std::shared_ptr<Object>> g_objects;
uint32_t g_next_id = 1;
std::unordered_map<uint32_t, uint32_t> g_queues;  // device id -> queue id

ErrorKind kind_of_scope(int filter) {
  if (filter == 1) return ErrorKind::OutOfMemory;
  if (filter == 2) return ErrorKind::Internal;
  return ErrorKind::Validation;
}

}  // namespace

uint32_t add_object(std::shared_ptr<Object> object) {
  const uint32_t id = g_next_id++;
  object->id = id;
  g_objects[id] = std::move(object);
  return id;
}

std::shared_ptr<Object> get_object(uint32_t id) {
  auto found = g_objects.find(id);
  return found == g_objects.end() ? nullptr : found->second;
}

std::shared_ptr<Object> get_object(uint32_t id, Kind kind) {
  std::shared_ptr<Object> object = get_object(id);
  if (object == nullptr || object->kind != kind) return nullptr;
  return object;
}

void remove_object(uint32_t id) { g_objects.erase(id); }

void device_children(uint32_t device_id, std::vector<uint32_t> *out) {
  out->clear();
  for (const auto &entry : g_objects) {
    if (entry.second->device_id == device_id) out->push_back(entry.first);
  }
}

SDL_GPUDevice *gpu_device() { return guidev::g_device; }

/* -- errors --------------------------------------------------------------- */

xt_value make_error(ErrorKind kind, const std::string &message) {
  xt_value error = xt_object_new_with_proto(error_proto((int)kind));
  xt_object_set(error, xt_string_from_cstr("message"), xt_string_from_cstr(message.c_str()));
  const char *name = kind == ErrorKind::OutOfMemory
                         ? "GPUOutOfMemoryError"
                         : (kind == ErrorKind::Internal ? "GPUInternalError" : "GPUValidationError");
  xt_object_set(error, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  xt_object_set(error, xt_string_from_cstr("__xt_webgpu_error"), xt_number((double)kind));
  return error;
}

xt_value make_device_lost(const std::string &reason) {
  xt_value info = xt_object_new_with_proto(device_lost_proto());
  xt_object_set(info, xt_string_from_cstr("reason"), xt_string_from_cstr(reason.c_str()));
  xt_object_set(info, xt_string_from_cstr("message"), xt_string_from_cstr(reason.c_str()));
  return info;
}

void record_error(Device &device, ErrorKind kind, const std::string &message) {
  for (auto scope = device.scopes.rbegin(); scope != device.scopes.rend(); ++scope) {
    if (kind_of_scope(scope->filter) != kind) continue;
    if (!scope->filled) {
      scope->filled = true;
      scope->kind = (int)kind;
      scope->message = message;
    }
    return;
  }
  dispatch_uncaptured_error(device, make_error(kind, message));
}

/* -- JS handles ----------------------------------------------------------- */

xt_value wrap_object(const std::shared_ptr<Object> &object, xt_value proto) {
  xt_value handle = xt_object_new_with_proto(proto);
  xt_object_set(handle, xt_string_from_cstr("__xt_webgpu_id"), xt_number((double)object->id));
  xt_object_set(handle, xt_string_from_cstr("__xt_webgpu_kind"), xt_number((double)object->kind));
  if (object->kind == Kind::Device) {
    auto *device = static_cast<Device *>(object.get());
    device->js = handle;
    xt_gc_add_root(&device->js);
  }
  return handle;
}

xt_value queue_object(uint32_t device_id) {
  auto found = g_queues.find(device_id);
  if (found != g_queues.end()) {
    std::shared_ptr<Object> existing = get_object(found->second, Kind::Queue);
    if (existing != nullptr) return wrap_object(existing, proto_for(Kind::Queue));
  }
  auto queue = std::make_shared<Object>();
  queue->kind = Kind::Queue;
  queue->device_id = device_id;
  const uint32_t id = add_object(queue);
  g_queues[device_id] = id;
  return wrap_object(queue, proto_for(Kind::Queue));
}

xt_value device_object(uint32_t device_id) {
  std::shared_ptr<Object> object = get_object(device_id, Kind::Device);
  if (object == nullptr) return XT_UNDEFINED;
  return static_cast<Device *>(object.get())->js;
}

}  // namespace webgpu
}  // namespace xtgui
