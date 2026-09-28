/*
 * xbintsc GUI engine — DOM event machinery (dispatch, listeners, propagation)
 * and the `Event` prototype methods.
 */

#include "dom_api_internal.h"

#include <string>
#include <vector>

namespace domapi {

static xt_value make_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                               xt_value legacy) {
  xt_value event = xt_object_new_with_proto(event_proto());
  xt_object_set(event, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(event, xt_string_from_cstr("target"), xt_gui_node_handle(win, target));
  xt_object_set(event, xt_string_from_cstr("defaultPrevented"), XT_FALSE);
  xt_object_set(event, xt_string_from_cstr("bubbles"), XT_TRUE);
  if (XT_IS_OBJECT(legacy)) {
    static const char *const fields[] = {
        "x",     "y",    "clientX", "clientY", "button", "clicks",
        "deltaX", "deltaY", "key",  "code",    "repeat", "ctrl",
        "shift", "alt",  "meta",
    };
    for (const char *field : fields) {
      xt_value value = xt_object_get_cstr(legacy, field);
      if (!XT_IS_UNDEFINED(value)) xt_object_set(event, xt_string_from_cstr(field), value);
    }
  }
  return event;
}

void remove_listener(XtGuiWindow *win, xtgui::Node *node, const std::string &type, xt_value fn,
                     bool capture) {
  auto found = win->node_listeners.find(node);
  if (found == win->node_listeners.end()) return;
  std::vector<XtGuiNodeListener> &list = found->second;
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (it->type == type && it->capture == capture && xt_truthy(xt_seq(it->fn, fn))) {
      list.erase(it);
      return;
    }
  }
}

static bool call_node_listeners(XtGuiWindow *win, xtgui::Node *node, const char *type,
                                xt_value event, bool capture) {
  auto found = win->node_listeners.find(node);
  if (found == win->node_listeners.end()) return false;
  std::vector<XtGuiNodeListener> snapshot = found->second;
  for (const XtGuiNodeListener &listener : snapshot) {
    if (listener.type != type || listener.capture != capture) continue;
    if (listener.once) remove_listener(win, node, listener.type, listener.fn, listener.capture);
    xt_value self = xt_gui_node_handle(win, node);
    xt_object_set(event, xt_string_from_cstr("currentTarget"), self);
    xt_value args[1] = {event};
    xt_call_with_this(listener.fn, self, 1, args);
    if (value_truthy(xt_object_get_cstr(event, "__stopImmediate"))) return true;
    if (value_truthy(xt_object_get_cstr(event, "__stop"))) return true;
  }
  return value_truthy(xt_object_get_cstr(event, "__stop"));
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(event_prevent_default) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("defaultPrevented"), XT_TRUE);
  return XT_UNDEFINED;
}

METHOD(event_stop_propagation) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("__stop"), XT_TRUE);
  return XT_UNDEFINED;
}

METHOD(event_stop_immediate_propagation) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_object_set(self, xt_string_from_cstr("__stop"), XT_TRUE);
  xt_object_set(self, xt_string_from_cstr("__stopImmediate"), XT_TRUE);
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace domapi

void xt_gui_emit_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                           xt_value legacy) {
  using namespace domapi;
  if (win == nullptr || win->document == nullptr || target == nullptr) return;
  if (win->node_listeners.empty()) return;
  std::vector<xtgui::Node *> path;
  for (xtgui::Node *node = const_cast<xtgui::Node *>(target); node != nullptr; node = node->parent) {
    path.push_back(node);
  }
  xt_value event = make_dom_event(win, type, target, legacy);
  /* Capture phase: root -> target. */
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    if (call_node_listeners(win, *it, type, event, true)) return;
  }
  /* Bubble phase: target -> root. */
  for (auto it = path.begin(); it != path.end(); ++it) {
    if (call_node_listeners(win, *it, type, event, false)) return;
  }
}

void xt_gui_dispatch_to_node(XtGuiWindow *win, const xtgui::Node *target, const char *type) {
  if (win == nullptr || target == nullptr) return;
  xt_value legacy = xt_object_new();
  xt_object_set(legacy, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  std::string describe = xtgui::xt_dom_describe(target);
  xt_object_set(legacy, xt_string_from_cstr("target"), xt_string_from_cstr(describe.c_str()));
  xt_gui_emit_dom_event(win, type, target, legacy);
}
