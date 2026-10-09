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

#include <algorithm>

#include "paint.h"
#include "text.h"

static xt_value g_window_proto = XT_UNDEFINED;

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
    /* Fresh document: invalidate every previously handed-out handle. */
    xt_gui_handles_reset(win);
    win->document = std::make_unique<xtgui::XtDocument>();
    float width = 0;
    float height = 0;
    xt_gui_window_viewport(win, &width, &height);
    win->document->load(html, width, height);
    win->geometry.dirty = 1;
    /* Run the AOT-compiled `<script>` bodies now that the tree is parsed, so
     * `document.getElementById(...)` resolves, then settle the first layout. */
    xt_gui_run_scripts(win);
    xt_gui_flush_dom(win);
  }
  xt_object_set(self, xt_string_from_cstr("__xt_gui_html"), xt_string_from_cstr(html.c_str()));
  /* Painting the document is milestone M3b+; the parsed tree and computed
   * styles are available now (see `computedStyle`). */
  xt_gui_emit(win, "DOMContentLoaded");
  xt_gui_emit(win, "load");
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
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("x"), xt_number(box->x));
  xt_object_set(object, xt_string_from_cstr("y"), xt_number(box->y));
  xt_object_set(object, xt_string_from_cstr("width"), xt_number(box->width));
  xt_object_set(object, xt_string_from_cstr("height"), xt_number(box->height));
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

/** Diagnostic/test hook: advance the CSS transition clock by `ms`. */
static xt_value win_advance(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr) return XT_UNDEFINED;
  double ms = argc > 0 ? xt_to_number(xt_arg(argc, argv, 0)) : 0.0;
  if (win->document->advance(ms)) win->geometry.dirty = 1;
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

static xt_value win_on(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc >= 2) {
    xt_value name = xt_to_string(xt_arg(argc, argv, 0));
    xt_value fn = xt_arg(argc, argv, 1);
    if (XT_IS_FUNCTION(fn)) {
      xt_value listeners = xt_object_get_cstr(self, "__xt_gui_events");
      if (!XT_IS_OBJECT(listeners)) {
        listeners = xt_object_new();
        xt_object_set(self, xt_string_from_cstr("__xt_gui_events"), listeners);
      }
      xt_value array = xt_object_get(listeners, name);
      if (!XT_IS_ARRAY(array)) {
        array = xt_array_new(0, NULL);
        xt_object_set(listeners, name, array);
      }
      xt_array_push(array, fn);
    }
  }
  return self;
}

static xt_value win_off(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (argc >= 2) {
    xt_value listeners = xt_object_get_cstr(self, "__xt_gui_events");
    if (XT_IS_OBJECT(listeners)) {
      xt_value name = xt_to_string(xt_arg(argc, argv, 0));
      xt_value fn = xt_arg(argc, argv, 1);
      xt_value array = xt_object_get(listeners, name);
      if (XT_IS_ARRAY(array)) {
        xt_value kept = xt_array_new(0, NULL);
        int32_t count = (int32_t)xt_to_number(xt_array_length(array));
        for (int32_t i = 0; i < count; i++) {
          xt_value candidate = xt_array_get(array, xt_number((double)i));
          if (candidate != fn) xt_array_push(kept, candidate);
        }
        xt_object_set(listeners, name, kept);
      }
    }
  }
  return self;
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
 * `win.sendEvent("keydown", { key, code })`. */
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
  define_method(proto, "advance", (void *)win_advance);
  define_method(proto, "measureText", (void *)win_measure_text);
  define_method(proto, "fontMetrics", (void *)win_font_metrics);
  define_method(proto, "close", (void *)win_close);
  define_method(proto, "isOpen", (void *)win_is_open);
  define_method(proto, "driver", (void *)win_driver);
  define_method(proto, "requestAnimationFrame", (void *)win_request_animation_frame);
  define_method(proto, "cancelAnimationFrame", (void *)win_cancel_animation_frame);
  define_method(proto, "on", (void *)win_on);
  define_method(proto, "off", (void *)win_off);
  define_method(proto, "hitTest", (void *)win_hit_test);
  define_method(proto, "sendEvent", (void *)win_send_event);
  g_window_proto = proto;
  return proto;
}
