/*
 * xbintsc GUI engine — input dispatch (pointer/wheel/key), pointer hover
 * tracking and the per-frame rendering of every open window.
 */

#include "gui_internal.h"

#include "paint.h"

#include <stdio.h>
#include <string.h>

#include <string>

using namespace guidev;

/* -- window events -------------------------------------------------------- */

void xt_gui_emit(XtGuiWindow *win, const char *event) {
  xt_gui_emit_payload(win, event, XT_UNDEFINED);
}

void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload) {
  if (win == NULL || !XT_IS_OBJECT(win->object)) return;
  xt_value listeners = xt_object_get_cstr(win->object, "__xt_gui_events");
  if (!XT_IS_OBJECT(listeners)) return;
  xt_value array = xt_object_get(listeners, xt_string_from_cstr(event));
  if (!XT_IS_ARRAY(array)) return;
  xt_value args[1];
  int32_t count = 0;
  if (!XT_IS_UNDEFINED(payload)) {
    args[0] = payload;
    count = 1;
  }
  int32_t listener_count = (int32_t)xt_to_number(xt_array_length(array));
  for (int32_t i = 0; i < listener_count; i++) {
    xt_value fn = xt_array_get(array, xt_number((double)i));
    if (XT_IS_FUNCTION(fn)) xt_call_with_this(fn, win->object, count, args);
  }
}

void xt_gui_flush_dom(XtGuiWindow *win) {
  if (win == NULL || win->document == nullptr) return;
  bool mutated = win->struct_dirty != 0;
  win->struct_dirty = 0;
  if (win->document->takeDirty()) mutated = true;
  if (!mutated) return;
  float width = 0;
  float height = 0;
  xt_gui_window_viewport(win, &width, &height);
  win->document->restyle(width, height);
  win->geometry.dirty = 1;
}

static XtGuiWindow *xt_gui_find_by_window_id(SDL_WindowID id) {
  SDL_Window *window = SDL_GetWindowFromID(id);
  if (window == NULL) return NULL;
  for (int i = 0; i < g_window_count; i++) {
    if (g_windows[i].window == window) return &g_windows[i];
  }
  return NULL;
}

/* -- input events --------------------------------------------------------- */

/** Deepest element under (x, y), or NULL. */
static const xtgui::Node *xt_gui_hit_node(XtGuiWindow *win, float x, float y) {
  if (win->document == nullptr) return nullptr;
  const xtgui::LayoutBox *box = win->document->layout().hitTest(x, y);
  const xtgui::Node *node = box != nullptr ? box->node : nullptr;
  while (node != nullptr && !node->isElement()) node = node->parent;
  return node;
}

/** Deepest element under (x, y) as a CSS-like descriptor, or an empty string. */
std::string xt_gui_hit_test(XtGuiWindow *win, float x, float y) {
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  return node == nullptr ? std::string() : xtgui::xt_dom_describe(node);
}

/** Update the hovered element from the pointer position; marks paint dirty. */
static void xt_gui_update_hover(XtGuiWindow *win, float x, float y) {
  if (win->document == nullptr) return;
  if (win->document->setHover(xt_gui_hit_node(win, x, y))) win->geometry.dirty = 1;
}

static xt_value xt_gui_pointer_payload(const char *type, XtGuiWindow *win, float x, float y,
                                       int button, int clicks) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(payload, xt_string_from_cstr("x"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("y"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("clientX"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("clientY"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("button"), xt_number(button));
  xt_object_set(payload, xt_string_from_cstr("clicks"), xt_number(clicks));
  std::string target = xt_gui_hit_test(win, x, y);
  if (!target.empty()) {
    xt_object_set(payload, xt_string_from_cstr("target"), xt_string_from_cstr(target.c_str()));
  }
  return payload;
}

void xt_gui_dispatch_pointer(XtGuiWindow *win, const char *type, float x, float y, int button,
                             int clicks) {
  if (win == NULL || !win->open) return;
  xt_gui_update_hover(win, x, y);
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  if (strcmp(type, "mousedown") == 0 && win->document != nullptr) {
    if (win->document->setFocus(node)) win->geometry.dirty = 1;
  }
  xt_value payload = xt_gui_pointer_payload(type, win, x, y, button, clicks);
  if (node != nullptr) xt_gui_emit_dom_event(win, type, node, payload);
  xt_gui_emit_payload(win, type, payload);
}

void xt_gui_dispatch_wheel(XtGuiWindow *win, float x, float y, float delta_x, float delta_y) {
  if (win == NULL || !win->open) return;
  xt_value payload = xt_gui_pointer_payload("wheel", win, x, y, -1, 0);
  xt_object_set(payload, xt_string_from_cstr("deltaX"), xt_number(delta_x));
  xt_object_set(payload, xt_string_from_cstr("deltaY"), xt_number(delta_y));
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  if (node != nullptr) xt_gui_emit_dom_event(win, "wheel", node, payload);
  xt_gui_emit_payload(win, "wheel", payload);
}

static xt_value xt_gui_key_payload(const char *type, const char *key, const char *code, bool repeat,
                                   bool ctrl, bool shift, bool alt, bool meta) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(payload, xt_string_from_cstr("key"), xt_string_from_cstr(key));
  xt_object_set(payload, xt_string_from_cstr("code"), xt_string_from_cstr(code));
  xt_object_set(payload, xt_string_from_cstr("repeat"), repeat ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("ctrl"), ctrl ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("shift"), shift ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("alt"), alt ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("meta"), meta ? XT_TRUE : XT_FALSE);
  return payload;
}

void xt_gui_dispatch_key(XtGuiWindow *win, const char *type, const char *key, const char *code) {
  if (win == NULL || !win->open) return;
  xt_value payload = xt_gui_key_payload(type, key, code, false, false, false, false, false);
  const xtgui::Node *node = win->document != nullptr ? win->document->focus() : nullptr;
  if (node != nullptr) xt_gui_emit_dom_event(win, type, node, payload);
  xt_gui_emit_payload(win, type, payload);
}

namespace guidev {

void handle_event(const SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    g_quit = 1;
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->window.windowID);
    if (win != NULL && win->open) xt_gui_quit_window(win);
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_RESIZED) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->window.windowID);
    if (win == NULL || !win->open) return;
    win->width = event->window.data1;
    win->height = event->window.data2;
    if (win->document != NULL) win->document->restyle((float)win->width, (float)win->height);
    win->geometry.dirty = 1;
    return;
  }

  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->motion.windowID);
    xt_gui_dispatch_pointer(win, "mousemove", event->motion.x, event->motion.y, -1, 0);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->button.windowID);
    /* SDL numbers buttons 1/2/3 (left/middle/right); DOM uses 0/1/2. */
    xt_gui_dispatch_pointer(win, "mousedown", event->button.x, event->button.y,
                            (int)event->button.button - 1, event->button.clicks);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->button.windowID);
    if (win == NULL || !win->open) return;
    int button = (int)event->button.button - 1;
    xt_gui_dispatch_pointer(win, "mouseup", event->button.x, event->button.y, button,
                            event->button.clicks);
    if (button == 0) {
      xt_gui_dispatch_pointer(win, "click", event->button.x, event->button.y, button,
                              event->button.clicks);
    }
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->wheel.windowID);
    /* DOM `deltaY` grows downwards; SDL's grows away from the user. */
    xt_gui_dispatch_wheel(win, event->wheel.mouse_x, event->wheel.mouse_y, event->wheel.x,
                          -event->wheel.y);
    return;
  }
  if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->key.windowID);
    if (win == NULL || !win->open) return;
    const char *type = event->type == SDL_EVENT_KEY_DOWN ? "keydown" : "keyup";
    xt_gui_emit_payload(win, type,
                        xt_gui_key_payload(type, SDL_GetKeyName(event->key.key),
                                           SDL_GetScancodeName(event->key.scancode),
                                           event->key.repeat, (event->key.mod & SDL_KMOD_CTRL) != 0,
                                           (event->key.mod & SDL_KMOD_SHIFT) != 0,
                                           (event->key.mod & SDL_KMOD_ALT) != 0,
                                           (event->key.mod & SDL_KMOD_GUI) != 0));
    return;
  }
}

}  // namespace guidev

/* -- rendering ------------------------------------------------------------ */

void xt_gui_run_animation_frames(XtGuiWindow *win, double timestamp_ms) {
  if (win == NULL || win->animation_frames.empty()) return;
  /* Swap the queue into a member, not a local: the GC root provider marks
   * `running_frames` too, so a collection triggered while one callback runs
   * cannot free the rest of the batch. Re-queueing from a callback lands in
   * `animation_frames` and runs on the *next* frame, not this one. */
  std::vector<XtGuiAnimationFrame> &frames = win->running_frames;
  frames.swap(win->animation_frames);
  for (const XtGuiAnimationFrame &frame : frames) {
    if (!win->open) break;
    xt_value arg = xt_number(timestamp_ms);
    xt_call_with_this(frame.fn, win->object, 1, &arg);
  }
  frames.clear();
}

void xt_gui_render_window(XtGuiWindow *win) {
  if (win == NULL || !win->open || win->window == NULL || g_device == NULL) return;
  float viewport_width = 0;
  float viewport_height = 0;
  xt_gui_window_viewport(win, &viewport_width, &viewport_height);

  /* Drive CSS transitions. */
  double now = (double)SDL_GetTicks();
  double delta = win->last_frame_ms > 0.0 ? now - win->last_frame_ms : 0.0;
  win->last_frame_ms = now;
  /* Animation-frame callbacks run before layout, so any DOM mutation they make
   * is picked up by this same frame. */
  xt_gui_run_animation_frames(win, now);
  /* A callback may have closed the window (directly or through the
   * auto-close guard): `xt_gui_quit_window` released the document and the GPU
   * geometry, so there is nothing left to lay out or paint. */
  if (!win->open) return;
  if (win->document != nullptr && win->document->advance(delta)) win->geometry.dirty = 1;

  /* A DOM mutation from a handler invalidates the tree: restyle/relayout
   * before (re)building the geometry. */
  xt_gui_flush_dom(win);

  /* Rebuild the geometry only when the document or viewport changed. */
  bool can_paint = xt_gui_renderer_ensure(g_device, win->window);
  if (can_paint && win->geometry.dirty && win->document != nullptr) {
    xtgui::DisplayList list;
    xtgui::xt_paint_build(win->document->layout().root(), list);
    xt_gui_geometry_upload(g_device, &win->geometry, list);
    float density = SDL_GetWindowPixelDensity(win->window);
    xt_gui_text_upload(g_device, &win->text_geometry, list, density > 0.0f ? density : 1.0f);
    xt_gui_image_upload(g_device, &win->image_geometry, list);
    win->geometry.dirty = 0;
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
  if (cmd == NULL) return;
  SDL_GPUTexture *swapchain = NULL;
  SDL_AcquireGPUSwapchainTexture(cmd, win->window, &swapchain, NULL, NULL);
  bool firstFrame = false;
  if (swapchain != NULL) {
    SDL_GPUColorTargetInfo target;
    memset(&target, 0, sizeof(target));
    target.texture = swapchain;
    target.clear_color.r = win->background[0];
    target.clear_color.g = win->background[1];
    target.clear_color.b = win->background[2];
    target.clear_color.a = win->background[3];
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    if (can_paint) {
      xt_gui_renderer_draw(cmd, pass, &win->geometry, viewport_width, viewport_height);
      xt_gui_text_draw(cmd, pass, &win->text_geometry, viewport_width, viewport_height);
      xt_gui_image_draw(cmd, pass, &win->image_geometry, viewport_width, viewport_height);
    }
    SDL_EndGPURenderPass(pass);
    firstFrame = !win->ready;
    win->ready = 1;
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  /* The first presented frame marks the window ready. Emitting after the
   * submit lets a `ready` handler safely close/destroy the window. */
  if (firstFrame) xt_gui_emit(win, "ready");
}
