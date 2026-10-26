/*
 * xbintsc GUI engine — `<canvas>` WebGL lifetime.
 *
 * A WebGL context is owned by the window, keyed by its DOM element (the same
 * shape as the 2D canvas and video tables). Its colour texture is what the
 * page's image pipeline samples, so no pixels pass through the CPU.
 */

#include <memory>

#include "gui_engine.h"
#include "gui_internal.h"

namespace {

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
  auto found = win->webgl_canvases.find(node);
  return found == win->webgl_canvases.end() ? nullptr : found->second.get();
}

xtgui::WebGLContext *xt_gui_webgl_get(XtGuiWindow *win, const xtgui::Node *node, bool webgl2) {
  if (win == nullptr || !isCanvasElement(node)) return nullptr;
  xtgui::WebGLContext *existing = xt_gui_webgl_find(win, node);
  if (existing != nullptr) return existing->is_webgl2() == webgl2 ? existing : nullptr;
  if (win->canvases.find(node) != win->canvases.end()) return nullptr;  // already a 2D canvas
  /* The runtime shader compiler targets Vulkan SPIR-V. A device whose backend
   * consumes another format (Metal, Direct3D 12) has no WebGL context in this
   * build, so `getContext("webgl")` answers null instead of handing out a
   * context whose draws could never create a pipeline. */
  if (guidev::g_device == nullptr ||
      (SDL_GetGPUShaderFormats(guidev::g_device) & SDL_GPU_SHADERFORMAT_SPIRV) == 0) {
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
  win->webgl_canvases[node] = std::move(context);
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, (float)raw->width(), (float)raw->height(), raw->id());
  }
  return raw;
}

void xt_gui_webgl_forget(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return;
  win->webgl_canvases.erase(node);
}

void xt_gui_webgl_shutdown(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->webgl_canvases.clear();
}

void xt_gui_webgl_submit_all() {
  for (int i = 0; i < XT_GUI_MAX_WINDOWS; i++) {
    XtGuiWindow &win = guidev::g_windows[i];
    if (!win.open) continue;
    for (auto &entry : win.webgl_canvases) entry.second->submit();
  }
}
