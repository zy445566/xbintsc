/*
 * xbintsc GUI engine — window methods.
 *
 * Every window returned by `createWindow` is a plain runtime object with the
 * engine's prototype. Methods use the compiled-function ABI
 * `xt_value fn(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv)`
 * and are wrapped into callable values with `xt_closure_new`, exactly like the
 * Node extension's objects.
 */

#include "gui_engine.h"
#include "listener_options.h"

#include <algorithm>

#include "image_api.h"
#include "paint.h"
#include "perf.h"
#include "text.h"

static xt_value g_window_proto = XT_UNDEFINED;

/** String value of a runtime value, as `std::string` ("" for null/undefined). */
static std::string window_string(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? data : "";
}

/* -- methods -------------------------------------------------------------- */

static xt_value win_set_title(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != NULL && win->window != NULL && argc >= 1) {
    const char *title = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
    SDL_SetWindowTitle(win->window, title);
  }
  return XT_UNDEFINED;
}

static xt_value win_set_size(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != NULL && win->window != NULL && argc >= 2) {
    SDL_SetWindowSize(win->window, (int)xt_to_number(xt_arg(argc, argv, 0)),
                      (int)xt_to_number(xt_arg(argc, argv, 1)));
  }
  return XT_UNDEFINED;
}

static xt_value win_load_html(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  std::string html;
  if (argc >= 1) {
    xt_value text = xt_to_string(xt_arg(argc, argv, 0));
    const char *data = xt_string_data(text);
    if (data != nullptr) html = data;
  }
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != nullptr) {
    win->html = html;
    /* A fresh document starts at the top of the page. */
    win->scroll_x = 0.0f;
    win->scroll_y = 0.0f;
    /* Fresh document: invalidate every previously handed-out handle, and drop
     * the decoders that belonged to the old tree. */
    xt_gui_handles_reset(win);
    xt_gui_media_shutdown(win);
    xt_gui_canvas_shutdown(win);
    win->document = std::make_unique<xtgui::XtDocument>();
    float width = 0;
    float height = 0;
    xt_gui_window_viewport(win, &width, &height);
    win->document->load(html, width, height);
    win->geometry.dirty = 1;
    win->present_dirty = 1;
    /* Run the AOT-compiled `<script>` bodies now that the tree is parsed, so
     * `document.getElementById(...)` resolves, then settle the first layout. */
    xt_gui_run_scripts(win);
    xt_gui_flush_dom(win);
    /* The document was replaced, so the frame loop must reconcile the media
     * and canvas tables with it on the next frame. `loadHTML` already flushed
     * the style/layout, so this is a sync request, not a restyle. */
    win->sync_dirty = 1;
  }
  xt_object_set(self, xt_string_from_cstr("__xt_gui_html"), xt_string_from_cstr(html.c_str()));
  /* Painting the document is milestone M3b+; the parsed tree and computed
   * styles are available now (see `computedStyle`). */
  xt_gui_emit(win, "DOMContentLoaded");
  xt_gui_emit(win, "load");
  /* A page that renders itself must start painting *before* its first
   * animation frame is requested — the `ready` event it waits for is fired by
   * the first presented frame, so waiting for a callback to start the pump
   * would deadlock. Programs that call `run()` keep their own loop; the pump
   * steps aside for it. */
  xt_browser_schedule_frame();
  return XT_UNDEFINED;
}

static xt_value win_get_html(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gui_html");
}

/** The `document` handle for this window (created on first access). */
static xt_value win_document_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_gui_document_handle(xt_gui_window_from_this(self));
}

/** Diagnostic/test hook: computed value of `property` for the first match of
 * `selector`, or an empty string. */
static xt_value win_computed_style(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 2) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  xt_value selectorValue = xt_to_string(xt_arg(argc, argv, 0));
  xt_value propertyValue = xt_to_string(xt_arg(argc, argv, 1));
  const char *selector = xt_string_data(selectorValue);
  const char *property = xt_string_data(propertyValue);
  if (selector == nullptr || property == nullptr) return xt_string_from_cstr("");
  const xtgui::Node *node = win->document->querySelector(selector);
  if (node == nullptr) return xt_string_from_cstr("");
  const xtgui::XtStyle *style = win->document->styleOf(node);
  if (style == nullptr) return xt_string_from_cstr("");
  std::string text = xtgui::xt_style_property_to_string(*style, property);
  return xt_string_from_cstr(text.c_str());
}

/** Diagnostic/test hook: serialized DOM. */
static xt_value win_document_tree(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  std::string text = win->document->toDebugString();
  return xt_string_from_cstr(text.c_str());
}

/** Diagnostic/test hook: number of elements matching a selector. */
static xt_value win_query_count(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return xt_number(0);
  xt_gui_flush_dom(win);
  xt_value selectorValue = xt_to_string(xt_arg(argc, argv, 0));
  const char *selector = xt_string_data(selectorValue);
  if (selector == nullptr) return xt_number(0);
  return xt_number((double)win->document->querySelectorAll(selector).size());
}

/** Diagnostic/test hook: laid-out border box of the first match of `selector`,
 * as `{ x, y, width, height }` (viewport-relative), or `undefined`. */
static xt_value win_get_bounding_rect(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  xt_gui_flush_dom(win);
  xt_value selectorValue = xt_to_string(xt_arg(argc, argv, 0));
  const char *selector = xt_string_data(selectorValue);
  if (selector == nullptr) return XT_UNDEFINED;
  const xtgui::Node *node = win->document->querySelector(selector);
  if (node == nullptr) return XT_UNDEFINED;
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  if (box == nullptr) return XT_UNDEFINED;
  /* Like the DOM: the rectangle of the *transformed* border box. */
  xtgui::LayoutRect rect = xtgui::xt_layout_transformed_bounds(box);
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("x"), xt_number(rect.x));
  xt_object_set(object, xt_string_from_cstr("y"), xt_number(rect.y));
  xt_object_set(object, xt_string_from_cstr("width"), xt_number(rect.width));
  xt_object_set(object, xt_string_from_cstr("height"), xt_number(rect.height));
  return object;
}

/** Diagnostic/test hook: serialized layout box tree. */
static xt_value win_layout_tree(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  std::string text = win->document->layout().dump();
  return xt_string_from_cstr(text.c_str());
}

static xt_value win_set_background(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != NULL && argc >= 1) {
    xt_value text = xt_to_string(xt_arg(argc, argv, 0));
    const char *data = xt_string_data(text);
    if (data != nullptr) {
      xtgui::Color color = xtgui::xt_css_parse_color(data);
      if (color.valid) {
        win->background[0] = color.r;
        win->background[1] = color.g;
        win->background[2] = color.b;
        win->background[3] = color.a;
        win->present_dirty = 1;
      }
    }
    xt_object_set(self, xt_string_from_cstr("__xt_gui_background"), text);
  }
  return XT_UNDEFINED;
}

/** Diagnostic/test hook: the display list built from the current layout. */
static xt_value win_paint_list(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  xtgui::DisplayList list;
  xtgui::xt_paint_build(win->document->layout().root(), list);
  std::string text = list.dump();
  return xt_string_from_cstr(text.c_str());
}

/** Diagnostic/test hook: number of shapes in the display list. */
static xt_value win_paint_count(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return xt_number(0);
  xt_gui_flush_dom(win);
  xtgui::DisplayList list;
  xtgui::xt_paint_build(win->document->layout().root(), list);
  return xt_number((double)list.rects.size());
}

/** Diagnostic/test hook: the serialized SVG document of the first `<svg>`
 * element matching `selector`, plus the intrinsic size and shape count the
 * NanoSVG parse yields. Returns `undefined` when there is no such element. */
static xt_value win_inline_svg(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return XT_UNDEFINED;
  xt_gui_flush_dom(win);
  xt_value selectorValue = xt_to_string(xt_arg(argc, argv, 0));
  const char *selector = xt_string_data(selectorValue);
  if (selector == nullptr) return XT_UNDEFINED;
  const xtgui::Node *node = win->document->querySelector(selector);
  const XtImageApi *images = xt_image_api();
  if (images == nullptr || !images->inline_is_root(node)) return XT_UNDEFINED;
  std::string markup = images->inline_serialize(node, &win->document->styles());
  int width = 0, height = 0;
  images->svg_markup_size(markup, &width, &height);
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("markup"), xt_string_from_cstr(markup.c_str()));
  xt_object_set(object, xt_string_from_cstr("width"), xt_number(width));
  xt_object_set(object, xt_string_from_cstr("height"), xt_number(height));
  xt_object_set(object, xt_string_from_cstr("shapes"),
                xt_number(images->svg_markup_shapes(markup)));
  return object;
}

/** Diagnostic/test hook: advance the CSS transition clock by `ms`. */
static xt_value win_advance(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  double ms = argc > 0 ? xt_to_number(xt_arg(argc, argv, 0)) : 0.0;
  if (win->document->advance(ms)) {
    win->geometry.dirty = 1;
    win->present_dirty = 1;
  }
  return XT_UNDEFINED;
}

/** Build a `FontSpec` from trailing method arguments: [text], size, family. */
static xtgui::FontSpec xt_gui_font_spec(int32_t argc, xt_value *argv, int sizeIndex, int familyIndex) {
  xtgui::FontSpec spec;
  if (argc > sizeIndex && XT_IS_NUMBER(xt_arg(argc, argv, sizeIndex))) {
    double size = xt_to_number(xt_arg(argc, argv, sizeIndex));
    if (size > 0) spec.pixel_size = (float)size;
  }
  if (argc > familyIndex && XT_IS_STRING(xt_arg(argc, argv, familyIndex))) {
    const char *family = xt_string_data(xt_arg(argc, argv, familyIndex));
    if (family != nullptr) spec.family = family;
  }
  return spec;
}

/** Diagnostic/test hook: shaped width of `text` at `fontSize` (optional) in
 * the given family (optional). */
static xt_value win_measure_text(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  std::string text;
  if (argc >= 1) {
    xt_value value = xt_to_string(xt_arg(argc, argv, 0));
    const char *data = xt_string_data(value);
    if (data != nullptr) text = data;
  }
  xtgui::FontSpec spec = xt_gui_font_spec(argc, argv, 1, 2);
  return xt_number((double)xtgui::xt_text_measure_width(text, spec));
}

/** Diagnostic/test hook: vertical metrics at `fontSize` (optional). */
static xt_value win_font_metrics(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xtgui::FontSpec spec = xt_gui_font_spec(argc, argv, 0, 1);
  float ascent = 0;
  float descent = 0;
  float lineHeight = 0;
  xtgui::xt_text_metrics(spec, &ascent, &descent, &lineHeight);
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("ascent"), xt_number(ascent));
  xt_object_set(object, xt_string_from_cstr("descent"), xt_number(descent));
  xt_object_set(object, xt_string_from_cstr("lineHeight"), xt_number(lineHeight));
  xt_object_set(object, xt_string_from_cstr("ready"), xtgui::xt_text_ready() ? XT_TRUE : XT_FALSE);
  return object;
}

static xt_value win_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_gui_quit_window(xt_gui_window_from_this(self));
  return XT_UNDEFINED;
}

static xt_value win_is_open(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  return (win != NULL && win->open) ? XT_TRUE : XT_FALSE;
}

/** Web 'window.closed': true once the window has been closed. The browser
 * spelling of 'isOpen()', so a document can stop its frame loop with
 * 'if (window.closed) return;' in every host. */
static xt_value win_closed_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  return (win != NULL && win->open) ? XT_FALSE : XT_TRUE;
}

/** `/driver`: which SDL_GPU backend the device is running (`"vulkan"`,
 * `"direct3d12"`, `"metal"`, …). The engine asks SDL for the best available
 * one, so this is how a program (or a test) confirms the path in use. */
static xt_value win_driver(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_string_from_cstr(xt_gui_driver());
}

/** `requestAnimationFrame(fn)` -> id. The callback runs once on the next frame
 * with the frame timestamp (ms) as its only argument. */
static xt_value win_request_animation_frame(xt_value self, xt_value env, int32_t argc,
                                            xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == NULL || !win->open) return xt_number(0);
  xt_value fn = xt_arg(argc, argv, 0);
  if (!XT_IS_FUNCTION(fn)) return xt_number(0);
  int id = win->next_animation_frame_id++;
  win->animation_frames.push_back(XtGuiAnimationFrame{id, fn});
  return xt_number((double)id);
}

/** `cancelAnimationFrame(id)` drops a callback queued for the next frame. */
static xt_value win_cancel_animation_frame(xt_value self, xt_value env, int32_t argc,
                                           xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == NULL || argc < 1) return XT_UNDEFINED;
  int id = (int)xt_to_number(xt_arg(argc, argv, 0));
  std::vector<XtGuiAnimationFrame> &frames = win->animation_frames;
  frames.erase(std::remove_if(frames.begin(), frames.end(),
                              [id](const XtGuiAnimationFrame &frame) { return frame.id == id; }),
               frames.end());
  return XT_UNDEFINED;
}


/* -- input diagnostics ---------------------------------------------------- */

static double xt_gui_number_or(xt_value value, double fallback) {
  return XT_IS_NUMBER(value) ? xt_to_number(value) : fallback;
}

/** Test/debug hook: CSS-like descriptor of the deepest element at (x, y). */
static xt_value win_hit_test(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 2) return xt_string_from_cstr("");
  xt_gui_flush_dom(win);
  float x = (float)xt_to_number(xt_arg(argc, argv, 0));
  float y = (float)xt_to_number(xt_arg(argc, argv, 1));
  std::string target = xt_gui_hit_test(win, x, y);
  return xt_string_from_cstr(target.c_str());
}

/** Test/debug hook: synthesise an input event and deliver it to handlers.
 * `win.sendEvent("click", { x, y, button, clicks })`,
 * `win.sendEvent("wheel", { x, y, deltaX, deltaY })` or
 * `win.sendEvent("keydown", { key, code })`. A key event may instead name the
 * SDL `scancode`/`keycode` plus modifiers to exercise a real-keyboard path. */
static xt_value win_send_event(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || argc < 1) return XT_FALSE;
  const char *type = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
  if (type == nullptr) return XT_FALSE;
  xt_value options = xt_arg(argc, argv, 1);
  double x = 0;
  double y = 0;
  if (XT_IS_OBJECT(options)) {
    x = xt_gui_number_or(xt_object_get_cstr(options, "x"), 0);
    y = xt_gui_number_or(xt_object_get_cstr(options, "y"), 0);
  }
  std::string name(type);
  if (name == "wheel") {
    double delta_x = 0;
    double delta_y = 0;
    if (XT_IS_OBJECT(options)) {
      delta_x = xt_gui_number_or(xt_object_get_cstr(options, "deltaX"), 0);
      delta_y = xt_gui_number_or(xt_object_get_cstr(options, "deltaY"), 0);
    }
    xt_gui_dispatch_wheel(win, (float)x, (float)y, (float)delta_x, (float)delta_y);
    return XT_TRUE;
  }
  if (name == "keydown" || name == "keyup") {
    if (XT_IS_OBJECT(options) && XT_IS_NUMBER(xt_object_get_cstr(options, "scancode"))) {
      int scancode = (int)xt_gui_number_or(xt_object_get_cstr(options, "scancode"), 0);
      int keycode = (int)xt_gui_number_or(xt_object_get_cstr(options, "keycode"), 0);
      auto flag = [options](const char *key) { return xt_truthy(xt_object_get_cstr(options, key)); };
      xt_gui_dispatch_physical_key(win, type, scancode, keycode, flag("repeat"), flag("ctrl"),
                                   flag("shift"), flag("alt"), flag("meta"), flag("caps"));
      return XT_TRUE;
    }
    const char *key = "";
    const char *code = "";
    if (XT_IS_OBJECT(options)) {
      xt_value key_value = xt_object_get_cstr(options, "key");
      xt_value code_value = xt_object_get_cstr(options, "code");
      if (XT_IS_STRING(key_value)) key = xt_string_data(key_value);
      if (XT_IS_STRING(code_value)) code = xt_string_data(code_value);
    }
    xt_gui_dispatch_key(win, type, key, code);
    return XT_TRUE;
  }
  int button = -1;
  int clicks = 0;
  if (XT_IS_OBJECT(options)) {
    button = (int)xt_gui_number_or(xt_object_get_cstr(options, "button"), -1);
    clicks = (int)xt_gui_number_or(xt_object_get_cstr(options, "clicks"), 0);
  }
  xt_gui_dispatch_pointer(win, type, (float)x, (float)y, button, clicks);
  return XT_TRUE;
}

/** Diagnostic/test hook: engine counters as one object. `heapBytes` and
 * `heapAllocations` are the live runtime heap, `treeNodes`/`detachedNodes` the
 * DOM tree and the detached pool, and `handles`/`listeners` the element-handle
 * tables. `detachedNodes` should stay flat in a steady state: a climbing value
 * means the program keeps removing nodes faster than it releases them. */
static xt_value win_stats(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("heapBytes"), xt_number((double)xt_heap_bytes()));
  xt_object_set(object, xt_string_from_cstr("heapAllocations"),
                xt_number((double)xt_heap_allocations()));
  double tree_nodes = 0;
  double detached_roots = 0;
  double detached_nodes = 0;
  if (win != nullptr && win->document != nullptr) {
    tree_nodes = (double)win->document->treeNodeCount();
    detached_roots = (double)win->document->detachedRootCount();
    detached_nodes = (double)win->document->detachedNodeCount();
  }
  xt_object_set(object, xt_string_from_cstr("treeNodes"), xt_number(tree_nodes));
  xt_object_set(object, xt_string_from_cstr("detachedRoots"), xt_number(detached_roots));
  xt_object_set(object, xt_string_from_cstr("detachedNodes"), xt_number(detached_nodes));
  xt_object_set(object, xt_string_from_cstr("handles"),
                xt_number(win != nullptr ? (double)win->node_handles.size() : 0));
  xt_object_set(object, xt_string_from_cstr("handleSlots"),
                xt_number(win != nullptr ? (double)win->node_order.size() : 0));
  xt_object_set(object, xt_string_from_cstr("listeners"),
                xt_number(win != nullptr ? (double)win->node_listeners.size() : 0));
  /* -- renderer counters (see `perf.h`) ---------------------------------- */
  /* The `*Ms` values describe the last completed frame; every other counter is
   * cumulative (diff two calls to get the work one frame did). */
  const xtgui::PerfTimings &ms = xtgui::g_perf_last;
  xt_object_set(object, xt_string_from_cstr("frameMs"), xt_number(ms.frame));
  xt_object_set(object, xt_string_from_cstr("restyleMs"), xt_number(ms.restyle));
  xt_object_set(object, xt_string_from_cstr("layoutMs"), xt_number(ms.layout));
  xt_object_set(object, xt_string_from_cstr("paintMs"), xt_number(ms.paint));
  xt_object_set(object, xt_string_from_cstr("uploadMs"), xt_number(ms.upload));
  xt_object_set(object, xt_string_from_cstr("drawMs"), xt_number(ms.draw));
  const xtgui::PerfCounters &counters = xtgui::g_perf_total;
  auto setCounter = [&object](const char *name, uint64_t value) {
    xt_object_set(object, xt_string_from_cstr(name), xt_number((double)value));
  };
  setCounter("frames", counters.frames);
  setCounter("presents", counters.presents);
  setCounter("exposes", counters.exposes);
  setCounter("deferred", counters.deferred);
  setCounter("starvedFrames", counters.starvedFrames);
  setCounter("skippedFrames", counters.skippedFrames);
  setCounter("restyles", counters.restyles);
  setCounter("restyleNodes", counters.restyleNodes);
  setCounter("rulesEvaluated", counters.rulesEvaluated);
  setCounter("mediaEvaluated", counters.mediaEvaluated);
  setCounter("layouts", counters.layouts);
  setCounter("layoutBoxes", counters.layoutBoxes);
  setCounter("textShapeCalls", counters.textShapeCalls);
  setCounter("textShapeHits", counters.textShapeHits);
  setCounter("textCacheBytes", xtgui::xt_text_cache_bytes());
  setCounter("textCacheEntries", xtgui::xt_text_cache_entries());
  setCounter("paints", counters.paints);
  setCounter("paintOps", counters.paintOps);
  setCounter("drawRuns", counters.drawRuns);
  setCounter("drawCalls", counters.drawCalls);
  setCounter("uploadSubmits", counters.uploadSubmits);
  setCounter("vertices", counters.vertices);
  setCounter("syncNodes", counters.syncNodes);
  setCounter("transitionFrames", counters.transitionFrames);
  setCounter("canvasDraws", counters.canvasDraws);
  setCounter("canvasUploads", counters.canvasUploads);
  setCounter("canvasUploadBytes", counters.canvasUploadBytes);
  setCounter("webglDraws", counters.webglDraws);
  setCounter("webglPasses", counters.webglPasses);
  setCounter("webglPipelineMisses", counters.webglPipelineMisses);
  setCounter("webglBufferUploadBytes", counters.webglBufferUploadBytes);
  return object;
}

/* -- prototype ------------------------------------------------------------ */

static void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

static void define_getter(xt_value proto, const char *name, void *fn) {
  xt_object_define_getter(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xt_value xt_gui_window_proto(void) {
  xt_gc_add_root(&g_window_proto);
  if (XT_IS_OBJECT(g_window_proto)) return g_window_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "setTitle", (void *)win_set_title);
  define_getter(proto, "document", (void *)win_document_get);
  define_method(proto, "setSize", (void *)win_set_size);
  define_method(proto, "loadHTML", (void *)win_load_html);
  define_method(proto, "getHTML", (void *)win_get_html);
  define_method(proto, "computedStyle", (void *)win_computed_style);
  define_method(proto, "documentTree", (void *)win_document_tree);
  define_method(proto, "queryCount", (void *)win_query_count);
  define_method(proto, "getBoundingClientRect", (void *)win_get_bounding_rect);
  define_method(proto, "layoutTree", (void *)win_layout_tree);
  define_method(proto, "setBackground", (void *)win_set_background);
  define_method(proto, "paintList", (void *)win_paint_list);
  define_method(proto, "paintCount", (void *)win_paint_count);
  define_method(proto, "inlineSvg", (void *)win_inline_svg);
  define_method(proto, "advance", (void *)win_advance);
  define_method(proto, "measureText", (void *)win_measure_text);
  define_method(proto, "fontMetrics", (void *)win_font_metrics);
  define_method(proto, "close", (void *)win_close);
  define_method(proto, "isOpen", (void *)win_is_open);
  define_getter(proto, "closed", (void *)win_closed_get);
  define_method(proto, "driver", (void *)win_driver);
  define_method(proto, "requestAnimationFrame", (void *)win_request_animation_frame);
  define_method(proto, "cancelAnimationFrame", (void *)win_cancel_animation_frame);
  /* `on` / `off` and their Web spellings (`addEventListener` /
   * `removeEventListener` with the listener options bag) live in
   * `window_events.cpp`. */
  xt_gui_install_window_events(proto);
  define_method(proto, "hitTest", (void *)win_hit_test);
  define_method(proto, "sendEvent", (void *)win_send_event);
  define_method(proto, "stats", (void *)win_stats);
  /* The browser surface a page written for a real page expects (`innerWidth`,
   * `setTimeout`, `navigator`, `matchMedia`, …) lives in `browser.cpp`. */
  xt_browser_install_window(proto);
  g_window_proto = proto;
  return proto;
}
