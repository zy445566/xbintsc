/*
 * xbintsc GUI engine — `element.dataset`.
 *
 * A `DOMStringMap` is a *view* over the element's `data-*` attributes:
 * `dataset.fooBar` reads `data-foo-bar`, assigning it writes the attribute and
 * `delete dataset.fooBar` removes it. The runtime's dynamic-property hooks make
 * that work for arbitrary keys without enumerating them.
 */

#include "dom_api_internal.h"

#include <cctype>
#include <string>

namespace domapi {

namespace {

/** Symbol-keyed hidden slot holding the element handle. A string key would show
 * up in `Object.keys(dataset)`, and `for (const k in dataset)` must only see the
 * data attributes. */
xt_value dataset_node_key() {
  static xt_value key = XT_UNDEFINED;
  if (key == XT_UNDEFINED) {
    xt_value description = xt_string_from_cstr("xbintsc.datasetNode");
    key = xt_symbol(1, &description);
    xt_gc_add_root(&key);
  }
  return key;
}

/** Resolve the element this DOMStringMap is a view of. Resolving the dataset
 * object itself would ask it for `__xt_gui_win`, a dynamic-key miss that would
 * re-enter the view. */
xtgui::Node *dataset_element(xt_value self, XtGuiWindow **win) {
  return resolve_node(xt_object_get(self, dataset_node_key()), win);
}

bool dataset_key(xt_value key, std::string *out) {
  if (!XT_IS_STRING(key)) return false;
  *out = to_string_value(key);
  /* Internal bookkeeping keys are never data attributes (and looking one up
   * must not re-enter the view). */
  if (out->compare(0, 6, "__xt_") == 0) return false;
  return !out->empty();
}

/** `fooBar` -> `foo-bar` (the attribute half of the DOMStringMap mapping). */
std::string to_attribute_name(const std::string &key) {
  std::string out;
  for (char c : key) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back('-');
      out.push_back((char)std::tolower((unsigned char)c));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

/** `foo-bar` -> `fooBar`. */
std::string to_property_name(const std::string &name) {
  std::string out;
  bool upper = false;
  for (char c : name) {
    if (c == '-') {
      upper = true;
      continue;
    }
    out.push_back(upper ? (char)std::toupper((unsigned char)c) : c);
    upper = false;
  }
  return out;
}

std::string attribute_of(const std::string &key) { return "data-" + to_attribute_name(key); }

const std::string *dataset_attribute(const xtgui::Node *node, const std::string &key) {
  return node != nullptr && node->isElement() ? node->attr(attribute_of(key)) : nullptr;
}

xt_value dataset_view_get(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!dataset_key(key, &name)) return XT_UNDEFINED;
  const std::string *value = dataset_attribute(dataset_element(self, nullptr), name);
  return value == nullptr ? XT_UNDEFINED : xt_string_from_cstr(value->c_str());
}

int dataset_view_set(xt_value self, xt_value key, xt_value value, void *data) {
  (void)data;
  std::string name;
  if (!dataset_key(key, &name)) return 0;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = dataset_element(self, &win);
  if (node == nullptr || !node->isElement()) return 1;
  set_attribute(win, node, attribute_of(name), to_string_value(value));
  return 1;
}

int dataset_view_has(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!dataset_key(key, &name)) return 0;
  return dataset_attribute(dataset_element(self, nullptr), name) != nullptr ? 1 : 0;
}

int dataset_view_remove(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!dataset_key(key, &name)) return 0;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = dataset_element(self, &win);
  if (node == nullptr || !node->isElement()) return 1;
  remove_attribute_raw(node, attribute_of(name));
  mark_dirty(win);
  return 1;
}

xt_value dataset_view_keys(xt_value self, void *data) {
  (void)data;
  xtgui::Node *node = dataset_element(self, nullptr);
  xt_value result = xt_array_new(0, nullptr);
  if (node == nullptr || !node->isElement()) return result;
  for (const xtgui::Attr &attr : node->attrs) {
    if (attr.name.size() <= 5 || attr.name.compare(0, 5, "data-") != 0) continue;
    std::string name = to_property_name(attr.name.substr(5));
    if (!name.empty()) xt_array_push(result, xt_string_from_cstr(name.c_str()));
  }
  return result;
}

const xt_object_hooks kDatasetHooks = {dataset_view_get, dataset_view_set, dataset_view_has,
                                       dataset_view_remove, dataset_view_keys};

}  // namespace

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(node_dataset_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value cached = xt_object_get_cstr(self, "__xt_gui_dataset");
  if (XT_IS_OBJECT(cached)) return cached;
  xt_value dataset = xt_object_new();
  xt_object_set(dataset, dataset_node_key(), self);
  xt_object_set_hooks(dataset, &kDatasetHooks, nullptr);
  xt_object_set(self, xt_string_from_cstr("__xt_gui_dataset"), dataset);
  return dataset;
}

}  // namespace domapi
