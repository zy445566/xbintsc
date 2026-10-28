/*
 * xbintsc GUI engine — input dispatch (pointer/wheel/key), pointer hover
 * tracking and the per-frame rendering of every open window.
 */

#include "gui_internal.h"
#include "listener_options.h"

#include "paint.h"
#include "perf.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include <string>

using namespace guidev;

/* -- window events -------------------------------------------------------- */

void xt_gui_emit(XtGuiWindow *win, const char *event) {
  xt_gui_emit_payload(win, event, XT_UNDEFINED);
}

/** Drop one window listener (by identity: type + fn + capture). */
static void remove_window_listener(XtGuiWindow *win, const XtGuiWindowListener &listener) {
  std::vector<XtGuiWindowListener> &listeners = win->window_listeners;
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if (it->type == listener.type && it->capture == listener.capture &&
        xt_truthy(xt_seq(it->fn, listener.fn))) {
      listeners.erase(it);
      return;
    }
  }
}

void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload) {
  if (win == NULL || !XT_IS_OBJECT(win->object)) return;
  xt_value args[1];
  int32_t count = 0;
  if (!XT_IS_UNDEFINED(payload)) {
    args[0] = payload;
    count = 1;
  }
  /* Snapshot the list: a handler may register or remove listeners while it
   * runs, and the DOM runs the batch as it stood when dispatch started. */
  std::vector<XtGuiWindowListener> snapshot = win->window_listeners;
  for (const XtGuiWindowListener &listener : snapshot) {
    if (listener.type != event) continue;
    /* `{ signal }` removes the listener at abort time (checked here, so no
     * callback plumbing is needed inside the signal). */
    if (xt_gui_signal_aborted(listener.signal)) {
      remove_window_listener(win, listener);
      continue;
    }
    if (listener.once) remove_window_listener(win, listener);
    if (XT_IS_FUNCTION(listener.fn)) xt_call_with_this(listener.fn, win->object, count, args);
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
  win->present_dirty = 1;
}

/* -- page scroll ---------------------------------------------------------- */

namespace {

void content_bounds(const xtgui::LayoutBox *box, float *max_x, float *max_y) {
  if (box == nullptr) return;
  float right = box->x + box->width;
  float bottom = box->y + box->height;
  if (right > *max_x) *max_x = right;
  if (bottom > *max_y) *max_y = bottom;
  for (const std::unique_ptr<xtgui::LayoutBox> &child : box->children) {
    content_bounds(child.get(), max_x, max_y);
  }
}

}  // namespace

void xt_gui_scroll_bounds(XtGuiWindow *win, float *max_x, float *max_y) {
  float content_x = 0;
  float content_y = 0;
  if (win != nullptr && win->document != nullptr) {
    xt_gui_flush_dom(win);
    content_bounds(win->document->layout().root(), &content_x, &content_y);
  }
  float viewport_width = 0;
  float viewport_height = 0;
  if (win != nullptr) xt_gui_window_viewport(win, &viewport_width, &viewport_height);
  *max_x = content_x > viewport_width ? content_x - viewport_width : 0.0f;
  *max_y = content_y > viewport_height ? content_y - viewport_height : 0.0f;
}

void xt_gui_scroll_to(XtGuiWindow *win, float x, float y) {
  if (win == nullptr) return;
  float max_x = 0;
  float max_y = 0;
  xt_gui_scroll_bounds(win, &max_x, &max_y);
  if (x < 0.0f) x = 0.0f;
  if (y < 0.0f) y = 0.0f;
  if (x > max_x) x = max_x;
  if (y > max_y) y = max_y;
  if (x == win->scroll_x && y == win->scroll_y) return;
  win->scroll_x = x;
  win->scroll_y = y;
  win->geometry.dirty = 1;
  win->present_dirty = 1;
  xt_gui_emit(win, "scroll");
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
  const xtgui::LayoutBox *box =
      win->document->layout().hitTest(x + win->scroll_x, y + win->scroll_y);
  const xtgui::Node *node = box != nullptr ? box->node : nullptr;
  while (node != nullptr && !node->isElement()) node = node->parent;
  return node;
}

/** Deepest element under (x, y) as a CSS-like descriptor, or an empty string. */
std::string xt_gui_hit_test(XtGuiWindow *win, float x, float y) {
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  return node == nullptr ? std::string() : xtgui::xt_dom_describe(node);
}

/** Update the hovered element from an already resolved target node; marks paint
 * dirty when the hover state (and therefore the styles) changed. */
static void xt_gui_update_hover(XtGuiWindow *win, const xtgui::Node *node) {
  if (win->document == nullptr) return;
  if (win->document->setHover(node)) {
    win->geometry.dirty = 1;
    win->present_dirty = 1;
  }
}

/** Pointer payload. `node` is the hit-test result the caller already resolved,
 * so one event does not walk the layout tree three times. */
static xt_value xt_gui_pointer_payload(const char *type, XtGuiWindow *win, float x, float y,
                                       int button, int clicks, const xtgui::Node *node) {
  (void)win;
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(payload, xt_string_from_cstr("x"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("y"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("clientX"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("clientY"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("button"), xt_number(button));
  xt_object_set(payload, xt_string_from_cstr("clicks"), xt_number(clicks));
  if (node != nullptr) {
    std::string target = xtgui::xt_dom_describe(node);
    if (!target.empty()) {
      xt_object_set(payload, xt_string_from_cstr("target"),
                    xt_string_from_cstr(target.c_str()));
    }
  }
  return payload;
}

void xt_gui_dispatch_pointer(XtGuiWindow *win, const char *type, float x, float y, int button,
                             int clicks) {
  if (win == NULL || !win->open) return;
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  xt_gui_update_hover(win, node);
  if (strcmp(type, "mousedown") == 0 && win->document != nullptr) {
    if (win->document->setFocus(node)) {
      win->geometry.dirty = 1;
      win->present_dirty = 1;
    }
  }
  xt_value payload = xt_gui_pointer_payload(type, win, x, y, button, clicks, node);
  if (node != nullptr) xt_gui_emit_dom_event(win, type, node, payload);
  xt_gui_emit_payload(win, type, payload);
}

void xt_gui_dispatch_wheel(XtGuiWindow *win, float x, float y, float delta_x, float delta_y) {
  if (win == NULL || !win->open) return;
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  xt_value payload = xt_gui_pointer_payload("wheel", win, x, y, -1, 0, node);
  xt_object_set(payload, xt_string_from_cstr("deltaX"), xt_number(delta_x));
  xt_object_set(payload, xt_string_from_cstr("deltaY"), xt_number(delta_y));
  if (node != nullptr) xt_gui_emit_dom_event(win, "wheel", node, payload);
  xt_gui_emit_payload(win, "wheel", payload);
  /* Default action: scroll the page (content that fits does not move). */
  xt_gui_scroll_to(win, win->scroll_x + delta_x, win->scroll_y + delta_y);
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
    win->present_dirty = 1;
    /* Web 'resize', so a document can re-measure on a window change in every
     * host (a browser already fires one). */
    xt_gui_emit(win, "resize");
    xt_gui_media_lists_notify(win);
    return;
  }
  /* A window the compositor revealed (uncovered, restored, shown) must be
   * redrawn even though nothing in the document changed: the frame loop only
   * presents on demand. */
  if (event->type == SDL_EVENT_WINDOW_EXPOSED || event->type == SDL_EVENT_WINDOW_SHOWN ||
      event->type == SDL_EVENT_WINDOW_RESTORED) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->window.windowID);
    if (win != NULL) win->present_dirty = 1;
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
  xtgui::xt_perf_frame_begin();
  float viewport_width = 0;
  float viewport_height = 0;
  xt_gui_window_viewport(win, &viewport_width, &viewport_height);

  /* Drive CSS transitions. */
  double now = (double)SDL_GetTicks();
  double delta = win->last_frame_ms > 0.0 ? now - win->last_frame_ms : 0.0;
  win->last_frame_ms = now;
  /* Animation-frame callbacks run before layout, so any DOM mutation they make
   * is picked up by this same frame. A page that asked for a frame expects it
   * to be presented even when its callback changed nothing visible. */
  if (!win->animation_frames.empty()) win->present_dirty = 1;
  xt_gui_run_animation_frames(win, now);
  /* A callback may have closed the window (directly or through the
   * auto-close guard): `xt_gui_quit_window` released the document and the GPU
   * geometry, so there is nothing left to lay out or paint. */
  if (!win->open) {
    xtgui::xt_perf_frame_end();
    return;
  }
  /* Reconcile the decoder/canvas tables with the live DOM only when the
   * document changed (or on the first frame): walking the whole tree every
   * frame was a per-frame cost proportional to the element count even when a
   * page had no `<video>` or `<canvas>` at all. */
  bool dom_changed = win->struct_dirty != 0 || win->sync_dirty != 0 || !win->ready ||
                     (win->document != nullptr && win->document->dirty());
  if (dom_changed) {
    xt_gui_media_sync(win);
    xt_gui_canvas_sync(win);
    win->sync_dirty = 0;
  }
  /* Playback advances every frame (it raises the media DOM events), but only a
   * decoder that produced a frame asks to present. */
  xt_gui_media_tick(win, now);
  if (win->document != nullptr && win->document->advance(delta)) {
    win->geometry.dirty = 1;
    win->present_dirty = 1;
    xtgui::g_perf_frame.transitionFrames += 1;
  }

  /* A DOM mutation from a handler invalidates the tree: restyle/relayout
   * before (re)building the geometry. */
  {
    xtgui::PerfScope scope(&xtgui::g_perf_frame_ms.restyle);
    xt_gui_flush_dom(win);
  }

  /* Rebuild the geometry only when the document or viewport changed. */
  bool can_paint = xt_gui_renderer_ensure(g_device, win->window);
  if (can_paint && win->geometry.dirty && win->document != nullptr) {
    {
      xtgui::PerfScope scope(&xtgui::g_perf_frame_ms.paint);
      /* Reuse the window's list: `clear()` keeps the payload vectors' capacity,
       * so a repaint does not reallocate every array from zero. */
      xtgui::DisplayList &list = win->display_list;
      list.clear();
      xtgui::xt_paint_build(win->document->layout().root(), list, viewport_width, viewport_height);
      xt_paint_translate(list, -win->scroll_x, -win->scroll_y);
      xtgui::g_perf_frame.paints += 1;
      xtgui::g_perf_frame.paintOps += (uint64_t)list.ops.size();
      {
        xtgui::PerfScope upload_scope(&xtgui::g_perf_frame_ms.upload);
        /* One command buffer for the frame's three vertex arrays (and the
         * glyph atlas), instead of one submit per buffer. */
        XtGuiUploadBatch uploads;
        xt_gui_upload_begin(&uploads, g_device);
        xt_gui_geometry_upload(g_device, &win->geometry, list, &uploads);
        float density = SDL_GetWindowPixelDensity(win->window);
        float text_scale = density > 0.0f ? density : 1.0f;
        xt_gui_text_upload(g_device, &win->text_geometry, list, text_scale, &uploads);
        xt_gui_image_upload(g_device, &win->image_geometry, list, text_scale, &uploads);
        xt_gui_upload_end(&uploads);
        xt_gui_build_runs(&win->geometry, &win->text_geometry, &win->image_geometry, list);
      }
      xtgui::g_perf_frame.drawRuns += (uint64_t)win->geometry.runs.size();
    }
    win->geometry.dirty = 0;
  }

  /* Upload any new video frame before the render command buffer is recorded,
   * so the draw samples the frame that just arrived. */
  bool uploaded = xt_gui_media_upload_frames(g_device);
  /* Canvases are CPU-drawn: push any new bitmap without a relayout. */
  if (xt_gui_canvas_upload_frames(g_device)) uploaded = true;
  /* WebGL contexts record into their own command buffers; submit them first so
   * the page's render pass samples this frame's drawing-buffer contents. */
  xt_gui_webgl_submit_all();
  if (uploaded || !win->webgl_canvases.empty()) win->present_dirty = 1;

  /* Present only when something happened. A static document otherwise burned a
   * swapchain acquire and a full-scene draw every frame. `XT_GUI_ALWAYS_PRESENT`
   * forces the old behaviour for debugging a missed invalidation. */
  static int always_present = -1;
  if (always_present < 0) {
    const char *value = SDL_getenv("XT_GUI_ALWAYS_PRESENT");
    always_present = (value != nullptr && value[0] != '\0' && strcmp(value, "0") != 0) ? 1 : 0;
  }
  if (win->ready && win->present_dirty == 0 && always_present == 0) {
    xtgui::g_perf_frame.skippedFrames += 1;
    xtgui::xt_perf_frame_end();
    return;
  }
  win->present_dirty = 0;

  bool firstFrame = false;
  {
    /* Scoped: the timing must be recorded before `xt_perf_frame_end` copies
     * the frame's phase timings. */
    xtgui::PerfScope draw_scope(&xtgui::g_perf_frame_ms.draw);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (cmd != NULL) {
      SDL_GPUTexture *swapchain = NULL;
      SDL_AcquireGPUSwapchainTexture(cmd, win->window, &swapchain, NULL, NULL);
      if (swapchain != NULL) {
        xtgui::g_perf_frame.presents += 1;
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
          float density = SDL_GetWindowPixelDensity(win->window);
          if (density <= 0.0f) density = 1.0f;
          xt_gui_renderer_draw(cmd, pass, &win->geometry, &win->text_geometry,
                               &win->image_geometry, viewport_width, viewport_height, density);
        }
        SDL_EndGPURenderPass(pass);
        firstFrame = !win->ready;
        win->ready = 1;
      }
      SDL_SubmitGPUCommandBuffer(cmd);
    }
  }
  /* The first presented frame marks the window ready. Emitting after the
   * submit lets a `ready` handler safely close/destroy the window. */
  if (firstFrame) xt_gui_emit(win, "ready");
  xtgui::xt_perf_frame_end();
}
