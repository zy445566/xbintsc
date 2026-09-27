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
    win->document = std::make_unique<xtgui::XtDocument>();
    float width = 0;
    float height = 0;
    xt_gui_window_viewport(win, &width, &height);
    win->document->load(html, width, height);
  }
  xt_object_set(self, xt_string_from_cstr("__xt_gui_html"), xt_string_from_cstr(html.c_str()));
  /* Painting the document is milestone M3b+; the parsed tree and computed
   * styles are available now (see `computedStyle`). */
  xt_gui_emit(win, "load");
  return XT_UNDEFINED;
}

static xt_value win_get_html(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gui_html");
}

/** Diagnostic/test hook: computed value of `property` for the first match of
 * `selector`, or an empty string. */
static xt_value win_computed_style(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 2) return xt_string_from_cstr("");
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
  std::string text = win->document->toDebugString();
  return xt_string_from_cstr(text.c_str());
}

/** Diagnostic/test hook: number of elements matching a selector. */
static xt_value win_query_count(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == nullptr || win->document == nullptr || argc < 1) return xt_number(0);
  xt_value selectorValue = xt_to_string(xt_arg(argc, argv, 0));
  const char *selector = xt_string_data(selectorValue);
  if (selector == nullptr) return xt_number(0);
  return xt_number((double)win->document->querySelectorAll(selector).size());
}

static xt_value win_set_background(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win != NULL && argc >= 1) {
    xt_object_set(self, xt_string_from_cstr("__xt_gui_background"), xt_to_string(xt_arg(argc, argv, 0)));
  }
  return XT_UNDEFINED;
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

/* -- prototype ------------------------------------------------------------ */

static void define_method(xt_value proto, const char *name, void *fn) {
  xt_object_set(proto, xt_string_from_cstr(name), xt_closure_new(fn, 0, NULL));
}

xt_value xt_gui_window_proto(void) {
  if (XT_IS_OBJECT(g_window_proto)) return g_window_proto;
  xt_value proto = xt_object_new();
  define_method(proto, "setTitle", (void *)win_set_title);
  define_method(proto, "setSize", (void *)win_set_size);
  define_method(proto, "loadHTML", (void *)win_load_html);
  define_method(proto, "getHTML", (void *)win_get_html);
  define_method(proto, "computedStyle", (void *)win_computed_style);
  define_method(proto, "documentTree", (void *)win_document_tree);
  define_method(proto, "queryCount", (void *)win_query_count);
  define_method(proto, "setBackground", (void *)win_set_background);
  define_method(proto, "close", (void *)win_close);
  define_method(proto, "isOpen", (void *)win_is_open);
  define_method(proto, "on", (void *)win_on);
  define_method(proto, "off", (void *)win_off);
  g_window_proto = proto;
  return proto;
}
