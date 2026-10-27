/*
 * xbintsc GUI engine — live `matchMedia` lists.
 *
 * `window.matchMedia(query)` returns one cached `MediaQueryList` per query. The
 * engine re-evaluates every live list on a window resize (the same evaluator
 * `@media` rules use) and dispatches a `change` event when the match flips, so
 * a page can react to a viewport change instead of polling `matches`.
 */

#include "gui_engine.h"


#include <string>
#include <vector>

#include "css.h"

namespace {

std::string string_value(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? data : "";
}

xt_value listeners_of(xt_value self, bool create) {
  xt_value listeners = xt_object_get_cstr(self, "__xt_gui_media_listeners");
  if (XT_IS_ARRAY(listeners) || !create) return listeners;
  listeners = xt_array_new(0, nullptr);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_media_listeners"), listeners);
  return listeners;
}

void add_listener(xt_value self, xt_value fn, bool once, xt_value signal) {
  if (!XT_IS_FUNCTION(fn)) return;
  xt_value entry = xt_object_new();
  xt_object_set(entry, xt_string_from_cstr("fn"), fn);
  xt_object_set(entry, xt_string_from_cstr("once"), once ? XT_TRUE : XT_FALSE);
  xt_object_set(entry, xt_string_from_cstr("signal"), signal);
  xt_array_push(listeners_of(self, true), entry);
}

void remove_listener(xt_value self, xt_value fn) {
  xt_value listeners = listeners_of(self, false);
  if (!XT_IS_ARRAY(listeners)) return;
  xt_value kept = xt_array_new(0, nullptr);
  int32_t count = (int32_t)xt_to_number(xt_array_length(listeners));
  for (int32_t i = 0; i < count; i++) {
    xt_value entry = xt_array_get(listeners, xt_number((double)i));
    if (!xt_truthy(xt_seq(xt_object_get_cstr(entry, "fn"), fn))) xt_array_push(kept, entry);
  }
  xt_object_set(self, xt_string_from_cstr("__xt_gui_media_listeners"), kept);
}

void call_change_listener(xt_value fn, xt_value list, xt_value event) {
  if (!XT_IS_FUNCTION(fn)) return;
  xt_value args[1] = {event};
  xt_call_with_this(fn, list, 1, args);
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(media_add_event_listener) {
  (void)env;
  if (argc < 2 || string_value(xt_arg(argc, argv, 0)) != "change") return XT_UNDEFINED;
  bool once = false;
  xt_value options = xt_arg(argc, argv, 2);
  if (XT_IS_OBJECT(options)) once = xt_truthy(xt_object_get_cstr(options, "once")) != 0;
  add_listener(self, xt_arg(argc, argv, 1), once, XT_UNDEFINED);
  return XT_UNDEFINED;
}

METHOD(media_remove_event_listener) {
  (void)env;
  if (argc < 2 || string_value(xt_arg(argc, argv, 0)) != "change") return XT_UNDEFINED;
  remove_listener(self, xt_arg(argc, argv, 1));
  return XT_UNDEFINED;
}

/** Legacy `addListener(fn)` / `removeListener(fn)`. */
METHOD(media_add_listener) {
  (void)env;
  if (argc >= 1) add_listener(self, xt_arg(argc, argv, 0), false, XT_UNDEFINED);
  return XT_UNDEFINED;
}

METHOD(media_remove_listener) {
  (void)env;
  if (argc >= 1) remove_listener(self, xt_arg(argc, argv, 0));
  return XT_UNDEFINED;
}

METHOD(media_onchange_get) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_object_get_cstr(self, "__xt_gui_media_onchange");
}

METHOD(media_onchange_set) {
  (void)env;
  if (argc >= 1) {
    xt_object_set(self, xt_string_from_cstr("__xt_gui_media_onchange"), xt_arg(argc, argv, 0));
  }
  return XT_UNDEFINED;
}

#undef METHOD

xt_value build_list(const std::string &query, bool matches) {
  xt_value list = xt_object_new();
  xt_object_set(list, xt_string_from_cstr("media"), xt_string_from_cstr(query.c_str()));
  xt_object_set(list, xt_string_from_cstr("matches"), matches ? XT_TRUE : XT_FALSE);
  xt_object_set(list, xt_string_from_cstr("__xt_gui_media_query"),
                xt_string_from_cstr(query.c_str()));
  xt_object_set(list, xt_string_from_cstr("addEventListener"),
                xt_closure_new((void *)media_add_event_listener, 0, nullptr));
  xt_object_set(list, xt_string_from_cstr("removeEventListener"),
                xt_closure_new((void *)media_remove_event_listener, 0, nullptr));
  xt_object_set(list, xt_string_from_cstr("addListener"),
                xt_closure_new((void *)media_add_listener, 0, nullptr));
  xt_object_set(list, xt_string_from_cstr("removeListener"),
                xt_closure_new((void *)media_remove_listener, 0, nullptr));
  xt_object_define_getter(list, xt_string_from_cstr("onchange"),
                          xt_closure_new((void *)media_onchange_get, 0, nullptr));
  xt_object_define_setter(list, xt_string_from_cstr("onchange"),
                          xt_closure_new((void *)media_onchange_set, 0, nullptr));
  return list;
}

}  // namespace

xt_value xt_browser_media_list(xt_value window, const std::string &query) {
  XtGuiWindow *win = xt_gui_window_from_this(window);
  if (win == nullptr) return build_list(query, false);
  auto cached = win->media_lists_by_query.find(query);
  if (cached != win->media_lists_by_query.end()) return cached->second;
  float width = 0.0f;
  float height = 0.0f;
  xt_gui_window_viewport(win, &width, &height);
  xt_value list = build_list(query, xtgui::xt_css_media_matches(query, width, height));
  win->media_lists.push_back(list);
  win->media_lists_by_query[query] = list;
  return list;
}

void xt_gui_media_lists_notify(XtGuiWindow *win) {
  if (win == nullptr) return;
  float width = 0.0f;
  float height = 0.0f;
  xt_gui_window_viewport(win, &width, &height);
  for (xt_value list : win->media_lists) {
    if (!XT_IS_OBJECT(list)) continue;
    std::string query = string_value(xt_object_get_cstr(list, "__xt_gui_media_query"));
    bool matches = xtgui::xt_css_media_matches(query, width, height);
    bool was = xt_truthy(xt_object_get_cstr(list, "matches"));
    xt_object_set(list, xt_string_from_cstr("matches"), matches ? XT_TRUE : XT_FALSE);
    if (was == matches) continue;
    xt_value event = xt_object_new();
    xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr("change"));
    xt_object_set(event, xt_string_from_cstr("media"), xt_string_from_cstr(query.c_str()));
    xt_object_set(event, xt_string_from_cstr("matches"), matches ? XT_TRUE : XT_FALSE);
    xt_value onchange = xt_object_get_cstr(list, "__xt_gui_media_onchange");
    call_change_listener(onchange, list, event);
    xt_value listeners = listeners_of(list, false);
    if (!XT_IS_ARRAY(listeners)) continue;
    xt_value snapshot = xt_array_new(0, nullptr);
    int32_t count = (int32_t)xt_to_number(xt_array_length(listeners));
    for (int32_t i = 0; i < count; i++) {
      xt_array_push(snapshot, xt_array_get(listeners, xt_number((double)i)));
    }
    for (int32_t i = 0; i < count; i++) {
      xt_value entry = xt_array_get(snapshot, xt_number((double)i));
      call_change_listener(xt_object_get_cstr(entry, "fn"), list, event);
    }
  }
}
