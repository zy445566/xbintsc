/*
 * xbintsc GUI engine — the browser globals a page expects to find.
 *
 * A page written for a real browser reaches the host through bare identifiers
 * (`window`, `document`, `navigator`, `localStorage`, `AudioContext`, …). The
 * gui extension declares those names as *global values* (see
 * `src/extensions/gui/index.ts`), so code generation lowers each one to a call
 * into this unit; nothing has to be declared in the source.
 *
 * Ordering: a module may read `window.innerWidth` or `document.getElementById`
 * while it initializes, which in a bundled program happens *before* the entry's
 * `createWindow(...)` statement runs. `xt_gui_ensure_window()` therefore opens
 * the window on first access, and `createWindow` adopts that window instead of
 * opening a second one (the page's handles would otherwise address the wrong
 * document). The document is still empty at that point, so
 * `getElementById`/`querySelector` answer with a *pending* handle that binds to
 * the real node once `loadHTML` has parsed the page — see `dom_api.cpp`.
 */

#include "gui_engine.h"
#include "dom_api_internal.h"

#include <cstdio>
#include <string>
#include <unordered_map>

namespace {

xt_value g_navigator = XT_UNDEFINED;
xt_value g_local_storage = XT_UNDEFINED;
xt_value g_location = XT_UNDEFINED;
xt_value g_media_list = XT_UNDEFINED;
xt_value g_document_global = XT_UNDEFINED;
/** Type name -> the constructor value handed out for it (built on first use). */
std::unordered_map<std::string, xt_value> g_types;
/** Window handle the globals were last installed on. */
xt_value g_installed_window = XT_UNDEFINED;

const char *cstr(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != NULL ? data : "";
}

xt_value string_of(const char *text) { return xt_string_from_cstr(text != NULL ? text : ""); }

xt_value fn_of(void *fn) { return xt_closure_new(fn, 0, NULL); }

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_method(xt_value target, const char *name, void *fn) { set_prop(target, name, fn_of(fn)); }

void set_number(xt_value target, const char *name, double value) {
  set_prop(target, name, xt_number(value));
}

xt_value noop(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

/* -- window --------------------------------------------------------------- */

double viewport_of(xt_value self, bool width) {
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == NULL) return 0.0;
  float w = 0.0f;
  float h = 0.0f;
  xt_gui_window_viewport(win, &w, &h);
  return width ? (double)w : (double)h;
}

/** `innerWidth` / `innerHeight`: the logical (CSS pixel) viewport. */
xt_value win_inner_width(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(viewport_of(self, true));
}

xt_value win_inner_height(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(viewport_of(self, false));
}

/** `devicePixelRatio`: the backing-store scale the renderer actually uses. */
xt_value win_device_pixel_ratio(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = xt_gui_window_from_this(self);
  float density = 1.0f;
  if (win != NULL && win->window != NULL) density = SDL_GetWindowPixelDensity(win->window);
  return xt_number(density > 0.0f ? (double)density : 1.0);
}

/** Timers are the runtime's own, so `window.setTimeout` and a bare
 * `setTimeout` share one queue (the gui loop services it every frame). */
xt_value win_set_timeout(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_set_timeout(argc, argv);
}

xt_value win_clear_timeout(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_clear_timeout(argc, argv);
}

xt_value win_set_interval(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_set_interval(argc, argv);
}

xt_value win_clear_interval(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_clear_interval(argc, argv);
}

xt_value win_queue_microtask(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return xt_queue_microtask(argc, argv);
}

xt_value win_navigator_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return g_navigator;
}

xt_value win_local_storage_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return g_local_storage;
}

xt_value win_location_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return g_location;
}

xt_value media_list_build(const std::string &query, bool matches);

/** `matchMedia(query)` -> a MediaQueryList-shaped object evaluated against the
 * current window's viewport, using the same evaluator as `@media` rules. The
 * list is a snapshot (there is no live change event), which matches the
 * engine's restyle-on-resize model. */
xt_value win_match_media(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  std::string query;
  if (argc >= 1) query = cstr(xt_arg(argc, argv, 0));
  XtGuiWindow *win = xt_gui_window_from_this(self);
  if (win == NULL) {
    xt_value handle = xt_gui_ensure_window();
    win = xt_gui_window_from_this(handle);
  }
  float width = 0.0f;
  float height = 0.0f;
  if (win != NULL) xt_gui_window_viewport(win, &width, &height);
  return media_list_build(query, xtgui::xt_css_media_matches(query, width, height));
}

/** `getComputedStyle(el, pseudoElt)`: a live view of the *cascaded* style, so
 * stylesheet rules (not only the inline `style` attribute) are visible; the
 * optional `::before`/`::after` selector reads generated content. */
xt_value win_get_computed_style(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  return domapi::computed_style_handle(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1));
}

xt_value win_alert(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argv;
  if (argc >= 1) fprintf(stderr, "%s\n", cstr(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

xt_value win_confirm(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_FALSE;
}

xt_value win_prompt(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

/* -- host objects --------------------------------------------------------- */

xt_value navigator_build(void) {
  xt_value nav = xt_object_new();
  /* A desktop agent string: a page that sniffs for mobile switches to its
   * touch layout, and a window here is driven by mouse and keyboard. */
  set_prop(nav, "userAgent",
           string_of("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 xbintsc"));
  set_prop(nav, "appVersion", string_of("5.0 (xbintsc)"));
  set_prop(nav, "platform", string_of("Linux x86_64"));
  set_prop(nav, "vendor", string_of("xbintsc"));
  set_prop(nav, "language", string_of("en-US"));
  set_number(nav, "hardwareConcurrency", 4);
  set_number(nav, "maxTouchPoints", 0);
  set_prop(nav, "onLine", XT_TRUE);
  set_prop(nav, "webdriver", XT_FALSE);
  set_method(nav, "vibrate", (void *)noop);
  xt_value languages = xt_array_new(0, NULL);
  xt_array_push(languages, string_of("en-US"));
  set_prop(nav, "languages", languages);
  return nav;
}

xt_value location_build(void) {
  xt_value location = xt_object_new();
  set_prop(location, "href", string_of("xbintsc://localhost/"));
  set_prop(location, "protocol", string_of("xbintsc:"));
  set_prop(location, "host", string_of("localhost"));
  set_prop(location, "hostname", string_of("localhost"));
  set_prop(location, "port", string_of(""));
  set_prop(location, "pathname", string_of("/"));
  set_prop(location, "search", string_of(""));
  set_prop(location, "hash", string_of(""));
  set_prop(location, "origin", string_of("xbintsc://localhost"));
  set_method(location, "reload", (void *)noop);
  set_method(location, "assign", (void *)noop);
  set_method(location, "replace", (void *)noop);
  set_prop(location, "toString", fn_of((void *)noop));
  return location;
}

xt_value media_list_build(const std::string &query, bool matches) {
  xt_value list = xt_object_new();
  set_prop(list, "media", xt_string_from_cstr(query.c_str()));
  set_prop(list, "matches", matches ? XT_TRUE : XT_FALSE);
  set_method(list, "addListener", (void *)noop);
  set_method(list, "removeListener", (void *)noop);
  set_method(list, "addEventListener", (void *)noop);
  set_method(list, "removeEventListener", (void *)noop);
  set_method(list, "dispatchEvent", (void *)noop);
  return list;
}

/* -- localStorage --------------------------------------------------------- */

xt_value storage_get_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value items = xt_object_get_cstr(self, "__items");
  xt_value value = xt_get(items, xt_to_string(xt_arg(argc, argv, 0)));
  return XT_IS_UNDEFINED(value) ? XT_NULL : xt_to_string(value);
}

xt_value storage_set_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value items = xt_object_get_cstr(self, "__items");
  xt_object_set(items, xt_to_string(xt_arg(argc, argv, 0)), xt_to_string(xt_arg(argc, argv, 1)));
  return XT_UNDEFINED;
}

xt_value storage_remove_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value items = xt_object_get_cstr(self, "__items");
  xt_delete(items, xt_to_string(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

xt_value storage_clear(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("__items"), xt_object_new());
  return XT_UNDEFINED;
}

/** The stored keys, in insertion order, as an array. */
xt_value storage_keys(xt_value self) {
  xt_value items = xt_object_get_cstr(self, "__items");
  return XT_IS_OBJECT(items) ? xt_object_keys(items) : xt_array_new(0, NULL);
}

xt_value storage_length_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_array_length(storage_keys(self));
}

/** `key(i)` — the i-th stored key, or `null` past the end. */
xt_value storage_key(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value keys = storage_keys(self);
  double at = argc > 0 ? xt_to_number(argv[0]) : -1;
  if (at < 0 || at >= (double)xt_array_size(keys)) return XT_NULL;
  return xt_array_get(keys, xt_number(at));
}

xt_value storage_build(void) {
  xt_value storage = xt_object_new();
  xt_object_set(storage, xt_string_from_cstr("__items"), xt_object_new());
  xt_object_define_getter(storage, xt_string_from_cstr("length"),
                          fn_of((void *)storage_length_get));
  set_method(storage, "getItem", (void *)storage_get_item);
  set_method(storage, "setItem", (void *)storage_set_item);
  set_method(storage, "removeItem", (void *)storage_remove_item);
  set_method(storage, "clear", (void *)storage_clear);
  set_method(storage, "key", (void *)storage_key);
  return storage;
}

/** The `escape` of Annex B: the legacy encoder `decodeURIComponent(escape(s))`
 * that libraries still use to turn a byte string back into text. */
xt_value host_type_construct(xt_value self, xt_value env, int32_t argc, xt_value *argv);

/** The prototype a DOM type name should inherit, so `el instanceof Element`
 * holds for every element handle the engine hands out. */
xt_value type_proto(const std::string &name) {
  if (name == "Event" || name == "CustomEvent" || name == "ProgressEvent") {
    return domapi::event_proto();
  }
  if (name == "AudioContext" || name == "webkitAudioContext") return xt_gui_audio_proto();
  if (name == "WebGLRenderingContext") return domapi::webgl_proto();
  if (name == "WebGL2RenderingContext") return domapi::webgl2_proto();
  if (name == "WebGLBuffer") return domapi::webgl_object_proto("buffer");
  if (name == "WebGLTexture") return domapi::webgl_object_proto("texture");
  if (name == "WebGLShader") return domapi::webgl_object_proto("shader");
  if (name == "WebGLProgram") return domapi::webgl_object_proto("program");
  if (name == "WebGLFramebuffer") return domapi::webgl_object_proto("framebuffer");
  if (name == "WebGLRenderbuffer") return domapi::webgl_object_proto("renderbuffer");
  if (name == "WebGLUniformLocation") return domapi::webgl_object_proto("uniform");
  if (name == "WebGLActiveInfo") return domapi::webgl_object_proto("info");
  if (name == "WebGLVertexArrayObject") return domapi::webgl_object_proto("vertexarray");
  if (name == "WebGLSampler") return domapi::webgl_object_proto("sampler");
  if (name == "WebGLQuery") return domapi::webgl_object_proto("query");
  if (name == "WebGLSync") return domapi::webgl_object_proto("sync");
  if (name == "WebGLTransformFeedback") return domapi::webgl_object_proto("transformfeedback");
  if (name == "ImageBitmap") {
    /* A bitmap is a plain immutable object, not a DOM node: a node proto would
     * expose the element width/height accessors and answer 0. */
    static xt_value bitmap_proto = XT_UNDEFINED;
    if (!XT_IS_OBJECT(bitmap_proto)) {
      bitmap_proto = xt_object_new();
      xt_gc_add_root(&bitmap_proto);
    }
    return bitmap_proto;
  }
  return domapi::node_proto();
}

/** Build the constructor value for `name` once and cache it. */
xt_value type_value(const std::string &name) {
  auto cached = g_types.find(name);
  if (cached != g_types.end()) return cached->second;
  xt_value env[1] = {xt_string_from_cstr(name.c_str())};
  xt_value ctor = xt_closure_new((void *)host_type_construct, 1, env);
  xt_function_set_metadata(ctor, xt_string_from_cstr(name.c_str()), 0);
  xt_value proto = type_proto(name);
  xt_function_set_prototype(ctor, proto);
  xt_object_set(proto, xt_string_from_cstr("constructor"), ctor);
  xt_value *slot = &g_types[name];
  *slot = ctor;
  xt_gc_add_root(slot);
  return ctor;
}

/** `new AudioContext()` builds a context, `new Event(type, init)` an event;
 * every other host type builds a plain object with the right prototype. */
xt_value host_type_construct(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  std::string name = cstr(xt_closure_env(env, 0));
  if (name == "AudioContext" || name == "webkitAudioContext") {
    return xt_gui_audio_create(argc, argv);
  }
  if (name == "Event" || name == "CustomEvent" || name == "ProgressEvent") {
    return domapi::event_construct(xt_arg(argc, argv, 0), xt_arg(argc, argv, 1));
  }
  xt_value target = xt_object_new_with_proto(type_proto(name));
  set_prop(target, "constructor", type_value(name));
  return target;
}

/** Host constructors are window members too — a page writes
 * `new window.AudioContext()` as often as `new AudioContext()`. */
void install_window_globals(xt_value win) {
  if (xt_seq(win, g_installed_window)) return;
  g_installed_window = win;
  xt_gc_add_root(&g_installed_window);
  if (XT_IS_OBJECT(g_navigator)) set_prop(win, "navigator", g_navigator);
  if (XT_IS_OBJECT(g_local_storage)) set_prop(win, "localStorage", g_local_storage);
  if (XT_IS_OBJECT(g_location)) set_prop(win, "location", g_location);
  set_prop(win, "AudioContext", type_value("AudioContext"));
  set_prop(win, "webkitAudioContext", type_value("webkitAudioContext"));
  set_prop(win, "Image", type_value("Image"));
  set_prop(win, "Audio", type_value("Audio"));
  set_prop(win, "Event", type_value("Event"));
  set_prop(win, "CustomEvent", type_value("CustomEvent"));
  set_prop(win, "WebGLRenderingContext", type_value("WebGLRenderingContext"));
  set_prop(win, "WebGL2RenderingContext", type_value("WebGL2RenderingContext"));
  set_prop(win, "isSecureContext", XT_TRUE);
  set_number(win, "screenX", 0);
  set_number(win, "screenY", 0);
}

/** Create the singletons the globals read through, on first use. */
void ensure_singletons(void) {
  if (!XT_IS_OBJECT(g_navigator)) {
    g_navigator = navigator_build();
    xt_gc_add_root(&g_navigator);
  }
  if (!XT_IS_OBJECT(g_local_storage)) {
    g_local_storage = storage_build();
    xt_gc_add_root(&g_local_storage);
  }
  if (!XT_IS_OBJECT(g_location)) {
    g_location = location_build();
    xt_gc_add_root(&g_location);
  }
  if (!XT_IS_OBJECT(g_media_list)) {
    g_media_list = media_list_build("", false);
    xt_gc_add_root(&g_media_list);
  }
}

}  // namespace

/* -- public entry points -------------------------------------------------- */

/** Install the browser window members on the `window` prototype. */
void xt_browser_install_window(xt_value proto) {
  ensure_singletons();
  xt_object_define_getter(proto, xt_string_from_cstr("innerWidth"), fn_of((void *)win_inner_width));
  xt_object_define_getter(proto, xt_string_from_cstr("innerHeight"),
                          fn_of((void *)win_inner_height));
  xt_object_define_getter(proto, xt_string_from_cstr("outerWidth"), fn_of((void *)win_inner_width));
  xt_object_define_getter(proto, xt_string_from_cstr("outerHeight"),
                          fn_of((void *)win_inner_height));
  xt_object_define_getter(proto, xt_string_from_cstr("devicePixelRatio"),
                          fn_of((void *)win_device_pixel_ratio));
  xt_object_define_getter(proto, xt_string_from_cstr("navigator"), fn_of((void *)win_navigator_get));
  xt_object_define_getter(proto, xt_string_from_cstr("localStorage"),
                          fn_of((void *)win_local_storage_get));
  xt_object_define_getter(proto, xt_string_from_cstr("location"), fn_of((void *)win_location_get));
  set_method(proto, "setTimeout", (void *)win_set_timeout);
  set_method(proto, "clearTimeout", (void *)win_clear_timeout);
  set_method(proto, "setInterval", (void *)win_set_interval);
  set_method(proto, "clearInterval", (void *)win_clear_interval);
  set_method(proto, "queueMicrotask", (void *)win_queue_microtask);
  set_method(proto, "matchMedia", (void *)win_match_media);
  set_method(proto, "getComputedStyle", (void *)win_get_computed_style);
  set_method(proto, "alert", (void *)win_alert);
  set_method(proto, "confirm", (void *)win_confirm);
  set_method(proto, "prompt", (void *)win_prompt);
  set_method(proto, "escape", (void *)xt_browser_escape);
  set_method(proto, "focus", (void *)noop);
  set_method(proto, "blur", (void *)noop);
  set_method(proto, "scrollTo", (void *)noop);
}

/** `window` / `self`. */
xt_value xt_browser_window(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value win = xt_gui_ensure_window();
  if (XT_IS_UNDEFINED(win)) return XT_UNDEFINED;
  install_window_globals(win);
  return win;
}

/** `document`. */
xt_value xt_browser_document(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value win = xt_browser_window(0, NULL);
  if (XT_IS_UNDEFINED(win)) return XT_UNDEFINED;
  xt_value doc = xt_gui_document_handle(xt_gui_window_from_this(win));
  if (!xt_seq(doc, g_document_global)) {
    g_document_global = doc;
    xt_gc_add_root(&g_document_global);
  }
  return doc;
}

xt_value xt_browser_navigator(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ensure_singletons();
  return g_navigator;
}

xt_value xt_browser_local_storage(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  ensure_singletons();
  return g_local_storage;
}

/** A global the host deliberately does not provide (`OffscreenCanvas`): it
 * answers `undefined`, so a `typeof` feature test takes the fallback path. */
xt_value xt_browser_missing(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

/** `Name` where `Name` is a DOM type: a constructor value for `instanceof`,
 * `typeof Name === "function"` probes and `new Name()`.
 *
 * The rendering types (`WebGLRenderingContext`, `OffscreenCanvas`, …) answer
 * `undefined` because there is no backend behind them: a library that probes
 * for them then takes its documented fallback instead of calling into stubs
 * that silently do nothing. */
xt_value xt_browser_type(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (argc < 1) return XT_UNDEFINED;
  std::string name = cstr(xt_arg(argc, argv, 0));
  if (name == "XRWebGLLayer" || name == "XRWebGLBinding" || name == "OffscreenCanvas" ||
      name == "ImageData" || name == "Response" ||
      name == "ReadableStream" || name == "Request" || name == "Headers" ||
      name == "DOMParser") {
    return XT_UNDEFINED;
  }
  return type_value(name);
}

/* -- global functions ----------------------------------------------------- */

/** `requestAnimationFrame(fn)`: queue the callback on the window and make sure
 * the frame pump is running. */
xt_value xt_browser_request_animation_frame(int32_t argc, xt_value *argv) {
  xt_value win = xt_browser_window(0, NULL);
  if (XT_IS_UNDEFINED(win)) return xt_number(0);
  xt_value id = xt_call_method(win, xt_string_from_cstr("requestAnimationFrame"), argc, argv);
  xt_browser_schedule_frame();
  return id;
}

xt_value xt_browser_cancel_animation_frame(int32_t argc, xt_value *argv) {
  xt_value win = xt_browser_window(0, NULL);
  if (XT_IS_UNDEFINED(win)) return XT_UNDEFINED;
  return xt_call_method(win, xt_string_from_cstr("cancelAnimationFrame"), argc, argv);
}

xt_value xt_browser_match_media(int32_t argc, xt_value *argv) {
  return win_match_media(XT_UNDEFINED, XT_UNDEFINED, argc, argv);
}

xt_value xt_browser_computed_style(int32_t argc, xt_value *argv) {
  return win_get_computed_style(XT_UNDEFINED, XT_UNDEFINED, argc, argv);
}



xt_value xt_browser_alert(int32_t argc, xt_value *argv) {
  return win_alert(XT_UNDEFINED, XT_UNDEFINED, argc, argv);
}

xt_value xt_browser_prompt(int32_t argc, xt_value *argv) {
  return win_prompt(XT_UNDEFINED, XT_UNDEFINED, argc, argv);
}
