/*
 * xbintsc GUI engine — `classList` and inline `style` object methods.
 */

#include "dom_api_internal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace domapi {

static std::vector<std::string> split_classes(const std::string &text) {
  std::vector<std::string> out;
  size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace((unsigned char)text[index])) index++;
    size_t start = index;
    while (index < text.size() && !std::isspace((unsigned char)text[index])) index++;
    if (index > start) out.push_back(text.substr(start, index - start));
  }
  return out;
}

static std::string join_classes(const std::vector<std::string> &names) {
  std::string out;
  for (size_t i = 0; i < names.size(); i++) {
    if (i > 0) out += " ";
    out += names[i];
  }
  return out;
}

static std::string inline_style_of(const xtgui::Node *node) {
  const std::string *value = node->attr("style");
  return value != nullptr ? *value : std::string();
}

static void write_declarations(xtgui::Node *node, const std::vector<xtgui::Declaration> &decls) {
  std::string out;
  for (const xtgui::Declaration &decl : decls) {
    if (!out.empty()) out += ";";
    out += decl.property + ":" + decl.value + (decl.important ? " !important" : "");
  }
  if (out.empty()) {
    remove_attribute_raw(node, "style");
  } else {
    set_attribute_raw(node, "style", out);
  }
}

static void set_style_property(XtGuiWindow *win, xtgui::Node *node, const std::string &name,
                               const std::string &value) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  bool found = false;
  for (xtgui::Declaration &decl : decls) {
    if (decl.property == name) {
      decl.value = value;
      decl.important = false;
      found = true;
    }
  }
  if (!found) decls.push_back({name, value, false});
  write_declarations(node, decls);
  mark_dirty(win);
}

static std::string get_style_property(const xtgui::Node *node, const std::string &name) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  std::string value;
  for (const xtgui::Declaration &decl : decls) {
    if (decl.property == name) value = decl.value;
  }
  return value;
}

static void remove_style_property(XtGuiWindow *win, xtgui::Node *node, const std::string &name) {
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  std::vector<xtgui::Declaration> kept;
  for (const xtgui::Declaration &decl : decls) {
    if (decl.property != name) kept.push_back(decl);
  }
  write_declarations(node, kept);
  mark_dirty(win);
}

static xtgui::Node *class_list_node(xt_value self, XtGuiWindow **winOut) {
  return resolve_node(xt_object_get_cstr(self, "__xt_gui_node"), winOut);
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(class_list_add) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr) return self;
  std::vector<std::string> names = split_classes(class_name_of(node));
  bool changed = false;
  for (int32_t i = 0; i < argc; i++) {
    std::string name = to_string_value(xt_arg(argc, argv, i));
    if (name.empty() || std::find(names.begin(), names.end(), name) != names.end()) continue;
    names.push_back(name);
    changed = true;
  }
  if (changed) set_class_name(win, node, join_classes(names));
  return self;
}

METHOD(class_list_remove) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr) return self;
  std::vector<std::string> names = split_classes(class_name_of(node));
  bool changed = false;
  for (int32_t i = 0; i < argc; i++) {
    std::string name = to_string_value(xt_arg(argc, argv, i));
    auto it = std::find(names.begin(), names.end(), name);
    if (it != names.end()) {
      names.erase(it);
      changed = true;
    }
  }
  if (changed) set_class_name(win, node, join_classes(names));
  return self;
}

METHOD(class_list_toggle) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = class_list_node(self, &win);
  if (node == nullptr || argc < 1) return XT_FALSE;
  std::string name = to_string_value(xt_arg(argc, argv, 0));
  std::vector<std::string> names = split_classes(class_name_of(node));
  auto it = std::find(names.begin(), names.end(), name);
  if (it != names.end()) {
    names.erase(it);
    set_class_name(win, node, join_classes(names));
    return XT_FALSE;
  }
  if (!name.empty()) {
    names.push_back(name);
    set_class_name(win, node, join_classes(names));
  }
  return XT_TRUE;
}

METHOD(class_list_contains) {
  (void)env;
  xtgui::Node *node = class_list_node(self, nullptr);
  if (node == nullptr || argc < 1) return XT_FALSE;
  std::string name = to_string_value(xt_arg(argc, argv, 0));
  std::vector<std::string> names = split_classes(class_name_of(node));
  return std::find(names.begin(), names.end(), name) != names.end() ? XT_TRUE : XT_FALSE;
}

static xtgui::Node *style_node(xt_value self, XtGuiWindow **winOut) {
  return resolve_node(xt_object_get_cstr(self, "__xt_gui_node"), winOut);
}

METHOD(style_set_property) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr || argc < 2) return XT_UNDEFINED;
  set_style_property(win, node, to_string_value(xt_arg(argc, argv, 0)),
                     to_string_value(xt_arg(argc, argv, 1)));
  return XT_UNDEFINED;
}

METHOD(style_get_property_value) {
  (void)env;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr || argc < 1) return xt_string_from_cstr("");
  return xt_string_from_cstr(
      get_style_property(node, to_string_value(xt_arg(argc, argv, 0))).c_str());
}

METHOD(style_remove_property) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr || argc < 1) return XT_UNDEFINED;
  remove_style_property(win, node, to_string_value(xt_arg(argc, argv, 0)));
  return XT_UNDEFINED;
}

/* -- named properties (`style.width = "50%"`) -----------------------------
 *
 * A CSSStyleDeclaration exposes every supported CSS property as a camel-cased
 * property (`style.width`, `style.backgroundColor`) as well as through
 * `setProperty`. There are hundreds of them, so instead of enumerating
 * accessors the style object installs the runtime's dynamic-property hooks:
 * a miss on the prototype is answered from the inline declaration block, and
 * an assignment writes it back there. That is what makes the common
 * `el.style.display = "none"` idiom affect layout instead of silently parking
 * a field on the JS object.
 */

namespace {

bool property_key(xt_value key, std::string *out) {
  if (!XT_IS_STRING(key)) return false;
  /* Named style properties are camel-cased (`style.backgroundColor`); the
   * declaration block stores the CSS spelling (`background-color`). Custom
   * properties keep their leading `--`, and internal bookkeeping keys stay
   * out of the view (looking one up must not re-enter it). */
  std::string raw = to_string_value(key);
  if (raw.compare(0, 6, "__xt_") == 0) return false;
  std::string mapped;
  for (char c : raw) {
    if (c >= 'A' && c <= 'Z') {
      mapped.push_back('-');
      mapped.push_back((char)std::tolower((unsigned char)c));
    } else {
      mapped.push_back(c);
    }
  }
  *out = xtgui::xt_css_lower(xtgui::xt_css_trim(mapped));
  return !out->empty();
}

xt_value style_view_get(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!property_key(key, &name)) return XT_UNDEFINED;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr) return xt_string_from_cstr("");
  return xt_string_from_cstr(get_style_property(node, name).c_str());
}

int style_view_set(xt_value self, xt_value key, xt_value value, void *data) {
  (void)data;
  std::string name;
  if (!property_key(key, &name)) return 0;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr) return 1;
  std::string text = to_string_value(value);
  if (text.empty()) {
    remove_style_property(win, node, name);
  } else {
    set_style_property(win, node, name, text);
  }
  return 1;
}

int style_view_has(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!property_key(key, &name)) return 0;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr) return 0;
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  for (const xtgui::Declaration &decl : decls) {
    if (decl.property == name) return 1;
  }
  return 0;
}

int style_view_remove(xt_value self, xt_value key, void *data) {
  (void)data;
  std::string name;
  if (!property_key(key, &name)) return 0;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = style_node(self, &win);
  if (node == nullptr) return 1;
  remove_style_property(win, node, name);
  return 1;
}

xt_value style_view_keys(xt_value self, void *data) {
  (void)data;
  xtgui::Node *node = style_node(self, nullptr);
  xt_value result = xt_array_new(0, nullptr);
  if (node == nullptr) return result;
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  for (const xtgui::Declaration &decl : decls) {
    xt_array_push(result, xt_string_from_cstr(decl.property.c_str()));
  }
  return result;
}

const xt_object_hooks kStyleViewHooks = {style_view_get, style_view_set, style_view_has,
                                         style_view_remove, style_view_keys};

}  // namespace

void install_style_hooks(xt_value style) {
  xt_object_set_hooks(style, &kStyleViewHooks, nullptr);
}

METHOD(style_length_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr) return xt_number(0);
  return xt_number(
      (double)xtgui::xt_css_parse_declarations(inline_style_of(node)).size());
}

/** `style.item(index)`: the property name at `index`, or "" past the end. */
METHOD(style_item) {
  (void)env;
  xtgui::Node *node = style_node(self, nullptr);
  if (node == nullptr || argc < 1) return xt_string_from_cstr("");
  std::vector<xtgui::Declaration> decls = xtgui::xt_css_parse_declarations(inline_style_of(node));
  double index = xt_to_number(xt_arg(argc, argv, 0));
  if (index < 0 || index >= (double)decls.size()) return xt_string_from_cstr("");
  return xt_string_from_cstr(decls[(size_t)index].property.c_str());
}

#undef METHOD

}  // namespace domapi
