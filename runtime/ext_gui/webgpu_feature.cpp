/*
 * xbintsc GUI engine — `webgpu` link feature registration.
 *
 * This translation unit is the only core-side place that references the WebGPU
 * context/DOM bindings. It is pulled out of `gui.a` only when
 * `webgpu_bridge.c` is linked, and installs `XtWebgpuApi` for the engine.
 * Without it the table is null, `getContext("webgpu")` answers null and
 * `navigator.gpu` is undefined, while the rest of the page works normally.
 */

#include "webgpu_api.h"

#include "webgpu.h"
#include "webgpu_bindings.h"

namespace {

const XtWebgpuApi *g_api = nullptr;

bool canvas_get(XtGuiWindow *win, const void *node, uint32_t *id) {
  return xtgui::webgpu::canvas_context(win, node, id);
}

bool canvas_find(XtGuiWindow *win, const void *node, uint32_t *id) {
  return xtgui::webgpu::canvas_find(win, node, id);
}

void canvas_forget(XtGuiWindow *win, const void *node) {
  xtgui::webgpu::canvas_forget(win, node);
}

void shutdown(XtGuiWindow *win) { xtgui::webgpu::canvas_shutdown(win); }

void info(uint32_t id, float *width, float *height, uint64_t *canvas_id) {
  if (!xtgui::webgpu::canvas_info(id, width, height, canvas_id)) {
    *width = 0.0f;
    *height = 0.0f;
    *canvas_id = 0;
  }
}

void resize(uint32_t id, int width, int height) {
  xtgui::webgpu::canvas_resize(id, width, height);
}

xt_value handle(xt_value self, uint32_t id) {
  return xtgui::webgpu::context_handle(self, id);
}

xt_value proto() { return xtgui::webgpu::context_proto(); }

xt_value navigator_gpu() { return xtgui::webgpu::navigator_gpu(); }

xt_value host_type(const char *name) { return xtgui::webgpu::host_type(name); }

void *color_texture(uint64_t canvas_id) {
  return xtgui::webgpu::canvas_color_texture(canvas_id);
}

const XtWebgpuApi kWebgpuApi = {
    canvas_get,  canvas_find, canvas_forget, shutdown,    info,
    resize,      handle,      proto,         navigator_gpu, host_type,
    color_texture,
};

}  // namespace

void xt_webgpu_api_install(const XtWebgpuApi *api) { g_api = api; }

const XtWebgpuApi *xt_webgpu_api() { return g_api; }

extern "C" void xt_webgpu_feature_register(void) {
  xt_webgpu_api_install(&kWebgpuApi);
}
