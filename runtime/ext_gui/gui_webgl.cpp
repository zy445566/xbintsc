/*
 * xbintsc GUI engine — `<canvas>` WebGL lifetime.
 *
 * A WebGL context is owned by the window, keyed by its DOM element (the same
 * shape as the 2D canvas and video tables). Its colour texture is what the
 * page's image pipeline samples, so no pixels pass through the CPU.
 */

#include <memory>
#include <unordered_map>

#include "gui_engine.h"
#include "gui_internal.h"
#include "webgl_feature.h"

namespace {

/** WebGL contexts are owned here rather than on `XtGuiWindow` so the core
 * window type does not need `webgl.h`. Entries are dropped by
 * `xt_gui_webgl_shutdown` when a window closes or reloads its document. */
std::unordered_map<XtGuiWindow *,
                   std::unordered_map<const xtgui::Node *, std::unique_ptr<xtgui::WebGLContext>>>
    g_webgl;

bool isCanvasElement(const xtgui::Node *node) {
  return node != nullptr && node->isElement() && node->isTag("canvas");
}

uint64_t next_webgl_id() {
  static uint64_t counter = 0;
  counter++;
  /* Keep WebGL ids out of the Canvas2D id space so the renderer can tell the
   * two kinds of canvas apart from the id alone. */
  return (1ULL << 32) | counter;
}

}  // namespace

xtgui::WebGLContext *xt_gui_webgl_find(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return nullptr;
  auto found = g_webgl.find(win);
  if (found == g_webgl.end()) return nullptr;
  auto entry = found->second.find(node);
  return entry == found->second.end() ? nullptr : entry->second.get();
}

xtgui::WebGLContext *xt_gui_webgl_get(XtGuiWindow *win, const xtgui::Node *node, bool webgl2) {
  if (win == nullptr || !isCanvasElement(node)) return nullptr;
  xtgui::WebGLContext *existing = xt_gui_webgl_find(win, node);
  if (existing != nullptr) return existing->is_webgl2() == webgl2 ? existing : nullptr;
  if (win->canvases.find(node) != win->canvases.end()) return nullptr;  // already a 2D canvas
  /* WebGL shaders reach SDL_GPU as SPIR-V (Vulkan, translated by glslang) or as
   * MSL (Metal, the same module cross-compiled by SPIRV-Cross). A device whose
   * backend consumes neither format — Direct3D 12 needs DXIL — has no WebGL
   * context in this build, so `getContext("webgl")` answers null instead of
   * handing out a context whose draws could never create a pipeline. */
  if (guidev::g_device == nullptr ||
      (SDL_GetGPUShaderFormats(guidev::g_device) &
       (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)) == 0) {
    return nullptr;
  }
  const std::string *width_attr = node->attr("width");
  const std::string *height_attr = node->attr("height");
  float width = 300.0f;
  float height = 150.0f;
  if (width_attr != nullptr) xtgui::xt_css_parse_number(*width_attr, &width);
  if (height_attr != nullptr) xtgui::xt_css_parse_number(*height_attr, &height);
  auto context = std::make_unique<xtgui::WebGLContext>(
      next_webgl_id(), (int)(width + 0.5f), (int)(height + 0.5f), webgl2);
  xtgui::WebGLContext *raw = context.get();
  g_webgl[win][node] = std::move(context);
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, (float)raw->width(), (float)raw->height(), raw->id());
  }
  return raw;
}

void xt_gui_webgl_forget(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return;
  auto found = g_webgl.find(win);
  if (found != g_webgl.end()) found->second.erase(node);
}

void xt_gui_webgl_shutdown(XtGuiWindow *win) {
  if (win == nullptr) return;
  g_webgl.erase(win);
}

bool xt_gui_webgl_submit_all() {
  bool any = false;
  for (auto &window : g_webgl) {
    for (auto &entry : window.second) {
      entry.second->submit();
      any = true;
    }
  }
  return any;
}
