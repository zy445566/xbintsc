/*
 * xbintsc GUI engine — form-control DOM bindings.
 *
 * Exposes the live value state (`value`, `checked`, selection range, `type`,
 * `disabled`, `placeholder`, ...) of `<input>`/`<textarea>`/`<select>` and the
 * small set of methods a form script uses (`setSelectionRange`, `select`).
 * Text editing itself lives in `form_input.cpp`; painting in `paint_form.cpp`.
 */

#include "dom_api_internal.h"

#include "form_state.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace domapi {

static void form_dirty(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->geometry.dirty = 1;
  win->present_dirty = 1;
}

static bool is_form_element(const xtgui::Node *node) {
  if (node == nullptr || !node->isElement()) return false;
  return xtgui::xt_form_is_control(node) || node->tag == "button" || node->tag == "option" ||
         node->tag == "optgroup" || node->tag == "fieldset" || node->tag == "label" ||
         node->tag == "form";
}

static bool text_control(const xtgui::Node *node) {
  return node != nullptr && xtgui::xt_form_is_text_like(node);
}

static int clamp_selection(const xtgui::Node *node, int value) {
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  std::string text = state != nullptr ? state->value : "";
  int length = (int)xtgui::xt_form_utf16_length(text);
  if (value < 0) return 0;
  if (value > length) return length;
  return value;
}

static void set_selection(xtgui::Node *node, int start, int end, int direction) {
  if (!text_control(node)) return;
  int length = (int)xtgui::xt_form_utf16_length(xtgui::xt_form_value(node));
  if (start < 0) start = 0;
  if (end < 0) end = 0;
  if (start > length) start = length;
  if (end > length) end = length;
  if (start > end) {
    int swap = start;
    start = end;
    end = swap;
    if (direction == 1) direction = -1;
  }
  if (direction == 0) direction = start < end ? 1 : 0;
  xtgui::FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  state->selection_start = start;
  state->selection_end = end;
  state->selection_direction = direction;
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(node_value_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_UNDEFINED;
  if (xtgui::xt_form_is_control(node)) {
    return xt_string_from_cstr(xtgui::xt_form_value(node).c_str());
  }
  if (node->isElement() && node->tag == "option") {
    return xt_string_from_cstr(xtgui::xt_form_option_value(node).c_str());
  }
  if (node->isElement() && node->tag == "button") {
    const std::string *value = node->attr("value");
    return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
  }
  return XT_UNDEFINED;
}

METHOD(node_value_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr) return XT_UNDEFINED;
  std::string value = argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "";
  if (xtgui::xt_form_is_control(node)) {
    xtgui::xt_form_set_value(node, value);
    form_dirty(win);
  } else if (node->isElement() && node->tag == "option") {
    set_attribute(win, node, "value", value);
  } else if (node->isElement() && node->tag == "button") {
    set_attribute(win, node, "value", value);
  }
  return XT_UNDEFINED;
}

METHOD(node_default_value_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_UNDEFINED;
  if (xtgui::xt_form_is_textarea(node)) {
    std::string text;
    for (const std::unique_ptr<xtgui::Node> &child : node->children) {
      if (child->isText()) text += child->text;
    }
    return xt_string_from_cstr(text.c_str());
  }
  if (xtgui::xt_form_is_control(node) || node->tag == "option") {
    const std::string *value = node->attr("value");
    return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
  }
  return XT_UNDEFINED;
}

METHOD(node_checked_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr) return XT_FALSE;
  return xtgui::xt_form_checked(node) ? XT_TRUE : XT_FALSE;
}

METHOD(node_checked_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !xtgui::xt_form_is_checkable(node)) return XT_UNDEFINED;
  xtgui::xt_form_set_checked(node, argc >= 1 ? value_truthy(xt_arg(argc, argv, 0)) : false);
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_type_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  std::string type = xtgui::xt_form_type(node);
  return xt_string_from_cstr(type.c_str());
}

METHOD(node_type_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  if (!xtgui::xt_form_is_input(node) && node->tag != "button" && node->tag != "select" &&
      node->tag != "textarea") {
    return XT_UNDEFINED;
  }
  set_attribute(win, node, "type", argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "text");
  if (node->form) node->form->initialized = false;
  if (xtgui::xt_form_is_control(node)) {
    xtgui::xt_form_init(node);
    form_dirty(win);
  }
  return XT_UNDEFINED;
}

METHOD(node_name_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  const std::string *value = node->attr("name");
  return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
}

METHOD(node_name_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  set_attribute(win, node, "name", argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "");
  return XT_UNDEFINED;
}

METHOD(node_disabled_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !is_form_element(node)) return XT_UNDEFINED;
  return xtgui::xt_form_is_disabled(node) ? XT_TRUE : XT_FALSE;
}

METHOD(node_disabled_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !is_form_element(node)) return XT_UNDEFINED;
  if (argc >= 1 && value_truthy(xt_arg(argc, argv, 0))) {
    set_attribute(win, node, "disabled", "");
  } else {
    remove_attribute_raw(node, "disabled");
    mark_dirty(win);
  }
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_read_only_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_UNDEFINED;
  return xtgui::xt_form_is_readonly(node) ? XT_TRUE : XT_FALSE;
}

METHOD(node_read_only_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_UNDEFINED;
  if (argc >= 1 && value_truthy(xt_arg(argc, argv, 0))) {
    set_attribute(win, node, "readonly", "");
  } else {
    remove_attribute_raw(node, "readonly");
    mark_dirty(win);
  }
  return XT_UNDEFINED;
}

METHOD(node_required_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_FALSE;
  return node->attr("required") != nullptr ? XT_TRUE : XT_FALSE;
}

METHOD(node_required_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_UNDEFINED;
  if (argc >= 1 && value_truthy(xt_arg(argc, argv, 0))) {
    set_attribute(win, node, "required", "");
  } else {
    remove_attribute_raw(node, "required");
    mark_dirty(win);
  }
  return XT_UNDEFINED;
}

METHOD(node_placeholder_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_UNDEFINED;
  const std::string *value = node->attr("placeholder");
  return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
}

METHOD(node_placeholder_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !xtgui::xt_form_is_control(node)) return XT_UNDEFINED;
  std::string value = argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "";
  if (value.empty()) {
    remove_attribute_raw(node, "placeholder");
    mark_dirty(win);
  } else {
    set_attribute(win, node, "placeholder", value);
  }
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_selection_start_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (!text_control(node)) return XT_UNDEFINED;
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  return xt_number(state != nullptr ? state->selection_start : 0);
}

METHOD(node_selection_start_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (!text_control(node)) return XT_UNDEFINED;
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  int end = state != nullptr ? state->selection_end : 0;
  int start = clamp_selection(node, argc >= 1 ? (int)xt_to_number(xt_arg(argc, argv, 0)) : 0);
  set_selection(node, start, end > start ? end : start, 1);
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_selection_end_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (!text_control(node)) return XT_UNDEFINED;
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  return xt_number(state != nullptr ? state->selection_end : 0);
}

METHOD(node_selection_end_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (!text_control(node)) return XT_UNDEFINED;
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  int start = state != nullptr ? state->selection_start : 0;
  int end = clamp_selection(node, argc >= 1 ? (int)xt_to_number(xt_arg(argc, argv, 0)) : 0);
  set_selection(node, start < end ? start : end, end, 1);
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_selection_direction_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (!text_control(node)) return XT_UNDEFINED;
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  int direction = state != nullptr ? state->selection_direction : 0;
  return xt_string_from_cstr(direction > 0 ? "forward" : direction < 0 ? "backward" : "none");
}

METHOD(node_set_selection_range) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (!text_control(node)) return XT_UNDEFINED;
  int start = argc >= 1 ? (int)xt_to_number(xt_arg(argc, argv, 0)) : 0;
  int end = argc >= 2 ? (int)xt_to_number(xt_arg(argc, argv, 1)) : start;
  std::string direction = argc >= 3 ? to_string_value(xt_arg(argc, argv, 2)) : "none";
  int sign = direction == "forward" ? 1 : direction == "backward" ? -1 : 0;
  set_selection(node, start, end, sign);
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_select) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (!text_control(node)) return XT_UNDEFINED;
  int length = (int)xtgui::xt_form_utf16_length(xtgui::xt_form_value(node));
  set_selection(node, 0, length, 1);
  xt_gui_set_focus(win, node);
  form_dirty(win);
  return XT_UNDEFINED;
}

METHOD(node_form_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr) return XT_UNDEFINED;
  for (xtgui::Node *parent = node->parent; parent != nullptr; parent = parent->parent) {
    if (parent->isElement() && parent->tag == "form") return xt_gui_node_handle(win, parent);
  }
  return XT_UNDEFINED;
}

METHOD(node_options_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xt_value array = xt_array_new(0, NULL);
  if (!xtgui::xt_form_is_select(node)) return array;
  std::vector<const xtgui::Node *> options;
  struct Collect {
    std::vector<const xtgui::Node *> *out;
    void visit(const xtgui::Node *current) const {
      if (current->isElement() && current->isTag("option")) out->push_back(current);
      for (const std::unique_ptr<xtgui::Node> &child : current->children) visit(child.get());
    }
  } collect = {&options};
  collect.visit(node);
  for (const xtgui::Node *option : options) xt_array_push(array, xt_gui_node_handle(win, option));
  return array;
}

METHOD(node_selected_index_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (!xtgui::xt_form_is_select(node)) return xt_number(-1);
  const xtgui::FormState *state = xtgui::xt_form_state(node);
  return xt_number(state != nullptr ? state->selected_index : -1);
}

METHOD(node_selected_index_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (!xtgui::xt_form_is_select(node)) return XT_UNDEFINED;
  xtgui::FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return XT_UNDEFINED;
  int index = argc >= 1 ? (int)xt_to_number(xt_arg(argc, argv, 0)) : -1;
  std::vector<const xtgui::Node *> options;
  struct Collect {
    std::vector<const xtgui::Node *> *out;
    void visit(const xtgui::Node *current) const {
      if (current->isElement() && current->isTag("option")) out->push_back(current);
      for (const std::unique_ptr<xtgui::Node> &child : current->children) visit(child.get());
    }
  } collect = {&options};
  collect.visit(node);
  if (index < 0 || (size_t)index >= options.size()) index = options.empty() ? -1 : 0;
  state->selected_index = index;
  if (index >= 0) state->value = xtgui::xt_form_option_value(options[(size_t)index]);
  form_dirty(win);
  return XT_UNDEFINED;
}

static xt_value attrum_get(xt_value self, int32_t argc, xt_value *argv, const char *name,
                           xt_value fallback) {
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return fallback;
  const std::string *value = node->attr(name);
  return xt_string_from_cstr(value != nullptr ? value->c_str() : "");
}

static xt_value attrum_set(xt_value self, int32_t argc, xt_value *argv, const char *name,
                           bool numeric) {
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  if (numeric && argc >= 1 && xt_to_number(xt_arg(argc, argv, 0)) < 0) {
    remove_attribute_raw(node, name);
    mark_dirty(win);
    return XT_UNDEFINED;
  }
  set_attribute(win, node, name, argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "");
  return XT_UNDEFINED;
}

METHOD(node_max_length_get) {
  (void)env;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return xt_number(-1);
  const std::string *value = node->attr("maxlength");
  return xt_number(value != nullptr ? std::strtod(value->c_str(), nullptr) : -1);
}

METHOD(node_max_length_set) {
  (void)env;
  return attrum_set(self, argc, argv, "maxlength", true);
}

METHOD(node_min_get) {
  (void)env;
  return attrum_get(self, argc, argv, "min", xt_string_from_cstr(""));
}

METHOD(node_min_set) {
  (void)env;
  return attrum_set(self, argc, argv, "min", false);
}

METHOD(node_max_get) {
  (void)env;
  return attrum_get(self, argc, argv, "max", xt_string_from_cstr(""));
}

METHOD(node_max_set) {
  (void)env;
  return attrum_set(self, argc, argv, "max", false);
}

METHOD(node_step_get) {
  (void)env;
  return attrum_get(self, argc, argv, "step", xt_string_from_cstr(""));
}

METHOD(node_step_set) {
  (void)env;
  return attrum_set(self, argc, argv, "step", false);
}

#undef METHOD

}  // namespace domapi
