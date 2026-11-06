/*
 * xbintsc GUI engine — interface between the WebGPU runtime and its JS bindings.
 */
#ifndef XT_GUI_WEBGPU_BINDINGS_H
#define XT_GUI_WEBGPU_BINDINGS_H

#include "webgpu.h"

namespace xtgui {
namespace webgpu {

using Handler = xt_value (*)(Object &, int argc, xt_value *argv);
using Getter = xt_value (*)(Object &);

/** Register a prototype member (called once from the unit that owns it). */
void add_method(Kind kind, const char *name, Handler handler);
void add_getter(Kind kind, const char *name, Getter getter);

void register_gpu_methods();
void register_resource_methods();
void register_buffer_methods();
void register_pipeline_methods();
void register_queue_methods();
void register_command_methods();
void register_canvas_methods();
void register_render_pass_methods();
void register_compute_pass_methods();

/** The prototype of one WebGPU interface. */
xt_value proto_for(Kind kind);
/** `GPUValidationError` / `GPUOutOfMemoryError` / `GPUInternalError`. */
xt_value error_proto(int kind);
xt_value device_lost_proto();
/** Fire `uncapturederror` on the device's listeners. */
void dispatch_uncaptured_error(Device &device, xt_value error);

/** Throw a JS TypeError from a binding. */
void throw_type_error(const std::string &message);
/** Record a validation error and return `fallback`. */
xt_value validation(Device &device, const std::string &message,
                    xt_value fallback = XT_UNDEFINED);

/** Resolve the device that owns `object`. */
std::shared_ptr<Device> device_for(const Object &object, std::string *error);
/** The JS handle for a registry id, or undefined. */
xt_value handle_of(uint32_t id);
/** A resolved promise, or a rejected one carrying a TypeError. */
xt_value resolved(xt_value value);

/** The JS device object's rooted listener list. */
xt_value listener_list(Device &device);

/** Parse the `entries` of a GPUBindGroup descriptor into a native bind group. */
bool parse_bind_group(Device &device, xt_value descriptor, BindGroup *out, std::string *error);

}  // namespace webgpu
}  // namespace xtgui

#endif /* XT_GUI_WEBGPU_BINDINGS_H */
