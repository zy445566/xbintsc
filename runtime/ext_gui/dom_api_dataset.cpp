/*
 * xbintsc GUI engine — `element.dataset`.
 *
 * A dataset object is a per-element cache whose known keys are live accessors
 * over the element's `data-*` attributes (`data-key-item` -> `keyItem`). Keys
 * assigned before the attribute exists become ordinary properties on that cache,
 * which is what a page that only reads the value back through `dataset` needs.
 */

#include <cctype>
#include <string>

#include "dom_api_internal.h"

namespace domapi {
namespace {

/** `data-key-item` -> `keyItem`. `name` is the attribute name after `data-`. */
std::string camel_case(const std::string &name) {
  std::string out;
  bool upper = false;
  for (char c : name) {
    if (c == '-') {
      upper = true;
    } else if (upper) {
      out.push_back((char)std::toupper((unsigned char)c));
      upper = false;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

xtgui::Node *element_from_env(xt_value env, XtGuiWindow **win) {
  return resolve_node(xt_closure_env(env, 0), win);
}

xt_value dataset_value_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = element_from_env(env, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  const std::string *value = node->attr(to_string_value(xt_closure_env(env, 1)));
  return value != nullptr ? xt_string_from_cstr(value->c_str()) : XT_UNDEFINED;
}

xt_value dataset_value_set(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = element_from_env(env, &win);
  if (node == nullptr || !node->isElement() || win == nullptr) return XT_UNDEFINED;
  std::string name = to_string_value(xt_closure_env(env, 1));
  set_attribute(win, node, name, argc >= 1 ? to_string_value(argv[0]) : std::string());
  return XT_UNDEFINED;
}

}  // namespace

xt_value node_dataset_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_dataset");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value dataset = xt_object_new();
  for (const xtgui::Attr &attr : node->attrs) {
    if (attr.name.rfind("data-", 0) != 0) continue;
    std::string key = camel_case(attr.name.substr(5));
    if (key.empty()) continue;
    xt_value environment[2] = {self, xt_string_from_cstr(attr.name.c_str())};
    xt_value getter = xt_closure_new((void *)dataset_value_get, 2, environment);
    xt_value setter = xt_closure_new((void *)dataset_value_set, 2, environment);
    xt_value keyValue = xt_string_from_cstr(key.c_str());
    xt_object_define_getter(dataset, keyValue, getter);
    xt_object_define_setter(dataset, keyValue, setter);
  }
  xt_object_set(self, xt_string_from_cstr("__xt_gui_dataset"), dataset);
  return dataset;
}

}  // namespace domapi
