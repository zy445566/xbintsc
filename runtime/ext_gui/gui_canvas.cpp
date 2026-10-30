/*
 * xbintsc GUI engine — `<canvas>` lifetime.
 *
 * A canvas bitmap is owned by the window, keyed by its DOM element (the same
 * shape as the `<video>` decoder table). This unit reconciles that table with
 * the live document, publishes each canvas's pixel size to the document so
 * layout can give the element its intrinsic box, and releases the matching GPU
 * texture when a canvas goes away.
 */

#include <memory>
#include <vector>

#include "canvas.h"
#include "document.h"
#include "gui_engine.h"
#include "gui_internal.h"
#include "perf.h"

namespace {

bool isCanvasElement(const xtgui::Node *node) {
  return node != nullptr && node->isElement() && node->isTag("canvas");
}

/** Canvas children are fallback content, so a canvas element ends the walk. */
void collectCanvases(const xtgui::Node *node, std::vector<const xtgui::Node *> *out) {
  if (node == nullptr) return;
  xtgui::g_perf_frame.syncNodes += 1;
  if (isCanvasElement(node)) {
    out->push_back(node);
    return;
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    collectCanvases(child.get(), out);
  }
}

int canvasAttributeSize(const xtgui::Node *node, const char *name, int fallback) {
  const std::string *attr = node->attr(name);
  if (attr == nullptr) return fallback;
  float value = 0.0f;
  if (!xtgui::xt_css_parse_number(*attr, &value) || value <= 0.0f) return fallback;
  return (int)(value + 0.5f);
}

void releaseCanvasTexture(xtgui::Canvas2D *canvas) {
  if (canvas != nullptr && guidev::g_device != nullptr) {
    xt_gui_canvas_texture_release(guidev::g_device, canvas->id());
  }
}

}  // namespace

xtgui::Canvas2D *xt_gui_canvas_find(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return nullptr;
  auto found = win->canvases.find(node);
  return found == win->canvases.end() ? nullptr : found->second.get();
}

xtgui::Canvas2D *xt_gui_canvas_get(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || !isCanvasElement(node)) return nullptr;
  auto found = win->canvases.find(node);
  if (found != win->canvases.end()) return found->second.get();
  int width = canvasAttributeSize(node, "width", 300);
  int height = canvasAttributeSize(node, "height", 150);
  auto canvas = std::make_unique<xtgui::Canvas2D>(width, height);
  xtgui::Canvas2D *raw = canvas.get();
  win->canvases[node] = std::move(canvas);
  /* A context obtained after the first frame must reach the display list
   * immediately, so `paintList`/the next paint sees its size and texture id. */
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, (float)width, (float)height, raw->id());
  }
  return raw;
}

void xt_gui_canvas_forget(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return;
  const XtWebglApi *webgl = xt_webgl_api();
  if (webgl != nullptr) webgl->forget(win, node);
  auto found = win->canvases.find(node);
  if (found == win->canvases.end()) return;
  releaseCanvasTexture(found->second.get());
  win->canvases.erase(found);
}

void xt_gui_canvas_sync(XtGuiWindow *win) {
  if (win == nullptr || win->document == nullptr) return;
  std::vector<const xtgui::Node *> nodes;
  collectCanvases(win->document->root(), &nodes);
  const XtWebglApi *webgl = xt_webgl_api();
  for (const xtgui::Node *node : nodes) {
    if (webgl != nullptr) {
      if (void *context = webgl->find(win, node)) {
        float width = 0.0f;
        float height = 0.0f;
        uint64_t id = 0;
        webgl->info(context, &width, &height, &id);
        win->document->setCanvasInfo(node, width, height, id);
        continue;
      }
    }
    xtgui::Canvas2D *canvas = xt_gui_canvas_find(win, node);
    if (canvas != nullptr) {
      win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                   canvas->id());
      continue;
    }
    /* No context yet: the attributes still define the intrinsic box, but there
     * is nothing to paint (a canvas with no context is transparent). */
    int width = canvasAttributeSize(node, "width", 300);
    int height = canvasAttributeSize(node, "height", 150);
    win->document->setCanvasInfo(node, (float)width, (float)height, 0);
  }
}

void xt_gui_canvas_bind_pending(XtGuiWindow *win, const xtgui::Node *node, xt_value handle) {
  if (win == nullptr || node == nullptr) return;
  xt_value pending = xt_object_get_cstr(handle, "__xt_gui_pending_canvas");
  if (!XT_IS_NUMBER(pending)) return;
  uint64_t id = (uint64_t)xt_to_number(pending);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_pending_canvas"), XT_UNDEFINED);
  auto found = win->pending_canvases.find(id);
  if (found == win->pending_canvases.end()) return;
  if (!isCanvasElement(node)) {
    win->pending_canvases.erase(found);
    return;
  }
  xtgui::Canvas2D *canvas = found->second.get();
  /* The parsed element's width/height attributes win over the default 300x150
   * a pre-load context started with. */
  int width = canvasAttributeSize(node, "width", canvas->width());
  int height = canvasAttributeSize(node, "height", canvas->height());
  if (width != canvas->width() || height != canvas->height()) canvas->resize(width, height);
  win->canvases[node] = std::move(found->second);
  win->pending_canvases.erase(found);
  if (win->document != nullptr) {
    win->document->setCanvasInfo(node, (float)canvas->width(), (float)canvas->height(),
                                 canvas->id());
  }
}

void xt_gui_canvas_shutdown(XtGuiWindow *win) {
  if (win == nullptr) return;
  const XtWebglApi *webgl = xt_webgl_api();
  if (webgl != nullptr) webgl->shutdown(win);
  for (auto &entry : win->canvases) releaseCanvasTexture(entry.second.get());
  win->canvases.clear();
  for (auto &entry : win->offscreen_canvases) releaseCanvasTexture(entry.second.get());
  win->offscreen_canvases.clear();
  /* `pending_canvases` survives `loadHTML`: a module that asked for a context
   * before the page existed still needs its bitmap when the parsed element
   * binds. They are released with the window record. */
}
