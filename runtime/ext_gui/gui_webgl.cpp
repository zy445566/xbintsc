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

namespace {

int canvasSizeAttr(const xtgui::Node *node, const char *name, int fallback) {
  const std::string *attr = node != nullptr ? node->attr(name) : nullptr;
  float value = 0.0f;
  if (attr != nullptr && xtgui::xt_css_parse_number(*attr, &value)) return (int)(value + 0.5f);
  return fallback;
}

bool spirvAvailable() {
  return guidev::g_device != nullptr &&
         (SDL_GetGPUShaderFormats(guidev::g_device) & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
}

}  // namespace

xtgui::WebGLContext *xt_gui_webgl_pending_create(XtGuiWindow *win, bool webgl2, int width, int height) {
  if (win == nullptr || !spirvAvailable()) return nullptr;
  auto context = std::make_unique<xtgui::WebGLContext>(
      next_webgl_id(), width > 0 ? width : 300, height > 0 ? height : 150, webgl2);
  xtgui::WebGLContext *raw = context.get();
  win->pending_webgl[raw->id()] = std::move(context);
  return raw;
}

void xt_gui_webgl_pending_resize(XtGuiWindow *win, xt_value handle, double width, double height,
                                 bool hasWidth, bool hasHeight) {
  if (win == nullptr) return;
  /* `handle` is the canvas element; the pending id lives on it. The WebGL
   * context handle itself carries `__xt_gl_context`, so accept both. */
  xt_value idValue = xt_object_get_cstr(handle, "__xt_gui_pending_webgl");
  if (!XT_IS_NUMBER(idValue)) idValue = xt_object_get_cstr(handle, "__xt_gl_context");
  if (!XT_IS_NUMBER(idValue)) return;
  auto found = win->pending_webgl.find((uint64_t)xt_to_number(idValue));
  if (found == win->pending_webgl.end()) return;
  xtgui::WebGLContext *context = found->second.get();
  context->resize(hasWidth ? (int)width : context->width(),
                  hasHeight ? (int)height : context->height());
}

void xt_gui_webgl_pending_size(XtGuiWindow *win, xt_value handle, double *width, double *height) {
  if (win == nullptr || width == nullptr || height == nullptr) return;
  xt_value idValue = xt_object_get_cstr(handle, "__xt_gui_pending_webgl");
  if (!XT_IS_NUMBER(idValue)) idValue = xt_object_get_cstr(handle, "__xt_gl_context");
  if (!XT_IS_NUMBER(idValue)) return;
  auto found = win->pending_webgl.find((uint64_t)xt_to_number(idValue));
  if (found == win->pending_webgl.end()) return;
  *width = (double)found->second->width();
  *height = (double)found->second->height();
}

void xt_gui_webgl_bind_pending(XtGuiWindow *win, const xtgui::Node *node, xt_value handle) {
  if (win == nullptr || node == nullptr) return;
  xt_value pending = xt_object_get_cstr(handle, "__xt_gui_pending_webgl");
  if (!XT_IS_NUMBER(pending)) return;
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_pending_webgl"), XT_UNDEFINED);
  auto found = win->pending_webgl.find((uint64_t)xt_to_number(pending));
  if (found == win->pending_webgl.end()) return;
  if (!isCanvasElement(node)) {
    win->pending_webgl.erase(found);
    return;
  }
  xtgui::WebGLContext *context = found->second.get();
  int width = canvasSizeAttr(node, "width", context->width());
  int height = canvasSizeAttr(node, "height", context->height());
  if (width != context->width() || height != context->height()) context->resize(width, height);
  win->webgl_canvases[node] = std::move(found->second);
  win->pending_webgl.erase(found);
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, (float)context->width(), (float)context->height(),
                                 context->id());
  }
}

void xt_gui_webgl_shutdown(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->webgl_canvases.clear();
  win->pending_webgl.clear();
}

void xt_gui_webgl_submit_all() {
  for (int i = 0; i < XT_GUI_MAX_WINDOWS; i++) {
    XtGuiWindow &win = guidev::g_windows[i];
    if (!win.open) continue;
    for (auto &entry : win.webgl_canvases) entry.second->submit();
  }
}
