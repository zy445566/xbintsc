/*
 * xbintsc GUI engine — text input, IME and the keyboard/pointer default actions
 * of form controls.
 *
 * SDL delivers *produced text* in `SDL_EVENT_TEXT_INPUT` (that is the only
 * source that survives a non-US layout or an IME) and pre-edit text in
 * `SDL_EVENT_TEXT_EDITING`. Key events handle the editing commands
 * (Backspace/arrows/clipboard); printable synthetic keys are inserted directly
 * so `win.sendEvent("keydown", { key: "a" })` works headlessly without waiting
 * for a matching text-input event.
 */

#include "gui_engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "form_state.h"
#include "layout.h"

namespace {

using xtgui::FormState;
using xtgui::Node;

bool form_editable(const Node *node) {
  return node != nullptr && xtgui::xt_form_is_text_like(node) &&
         !xtgui::xt_form_is_disabled(node) && !xtgui::xt_form_is_readonly(node);
}

FormState *focused_state(XtGuiWindow *win, Node **node_out) {
  if (node_out != nullptr) *node_out = nullptr;
  if (win == nullptr || win->document == nullptr) return nullptr;
  Node *node = const_cast<Node *>(win->document->focus());
  if (!form_editable(node)) return nullptr;
  if (node_out != nullptr) *node_out = node;
  return xtgui::xt_form_state(node);
}

void form_dirty(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->geometry.dirty = 1;
  win->present_dirty = 1;
}

xt_value make_payload(const char *type) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  return payload;
}

void emit_input(XtGuiWindow *win, Node *node, const std::string &data, const char *input_type) {
  xt_value payload = make_payload("input");
  xt_object_set(payload, xt_string_from_cstr("data"), xt_string_from_cstr(data.c_str()));
  xt_object_set(payload, xt_string_from_cstr("inputType"),
                xt_string_from_cstr(input_type));
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xtgui::xt_form_value(node).c_str()));
  xt_object_set(payload, xt_string_from_cstr("isComposing"), XT_FALSE);
  xt_gui_emit_dom_event(win, "input", node, payload);
}

void emit_composition(XtGuiWindow *win, Node *node, const char *type, const std::string &data) {
  xt_value payload = make_payload(type);
  xt_object_set(payload, xt_string_from_cstr("data"), xt_string_from_cstr(data.c_str()));
  xt_object_set(payload, xt_string_from_cstr("isComposing"),
                std::string(type) == "compositionend" ? XT_FALSE : XT_TRUE);
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xtgui::xt_form_value(node).c_str()));
  xt_gui_emit_dom_event(win, type, node, payload);
}

/** Emit `keypress`; true when a listener cancelled the insertion. */
bool emit_keypress(XtGuiWindow *win, Node *node, const std::string &text) {
  if (text.empty()) return false;
  xt_value payload = make_payload("keypress");
  xt_object_set(payload, xt_string_from_cstr("key"), xt_string_from_cstr(text.c_str()));
  unsigned char first = (unsigned char)text[0];
  double code = first;
  xt_object_set(payload, xt_string_from_cstr("charCode"), xt_number(code));
  xt_object_set(payload, xt_string_from_cstr("keyCode"), xt_number(code));
  return xt_gui_emit_dom_event(win, "keypress", node, payload);
}

int max_length_of(const Node *node) {
  const std::string *value = node->attr("maxlength");
  if (value == nullptr) return -1;
  return (int)std::strtod(value->c_str(), nullptr);
}

/** Drop characters a browser's number/date/time editors would not accept. */
std::string filter_for_type(const Node *node, const std::string &text) {
  std::string type = xtgui::xt_form_type(node);
  std::string out;
  for (char c : text) {
    bool keep = true;
    if (type == "number") {
      keep = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
    } else if (type == "date" || type == "month" || type == "week") {
      keep = (c >= '0' && c <= '9') || c == '-' || c == '/';
    } else if (type == "time") {
      keep = (c >= '0' && c <= '9') || c == ':' || c == '.';
    } else if (type == "datetime-local") {
      keep = (c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T' || c == ' ';
    }
    if (keep) out.push_back(c);
  }
  return out;
}

/** Replace `[start, end)` (UTF-16 units) with `text`. Returns the new caret. */
int replace_range(Node *node, int start, int end, std::string text) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return 0;
  text = filter_for_type(node, text);
  int length = (int)xtgui::xt_form_utf16_length(state->value);
  if (start < 0) start = 0;
  if (end < start) end = start;
  if (end > length) end = length;
  int max_length = max_length_of(node);
  if (max_length >= 0) {
    int current = length - (end - start);
    int available = max_length - current;
    if (available < 0) available = 0;
    size_t units = xtgui::xt_form_utf16_length(text);
    if ((int)units > available) text = xtgui::xt_form_utf16_slice(text, 0, (size_t)available);
  }
  std::string before = xtgui::xt_form_utf16_slice(state->value, 0, (size_t)start);
  std::string after = xtgui::xt_form_utf16_slice(state->value, (size_t)end, (size_t)length);
  state->value = before + text + after;
  int caret = start + (int)xtgui::xt_form_utf16_length(text);
  state->selection_start = caret;
  state->selection_end = caret;
  state->selection_direction = 0;
  return caret;
}

int selection_length(const FormState *state) {
  return state->selection_end - state->selection_start;
}

void commit_composition(XtGuiWindow *win, Node *node, const std::string &text) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  int start = state->selection_start;
  int end = state->selection_end;
  state->composing = false;
  state->composition.clear();
  if (!text.empty()) {
    replace_range(node, start, end, text);
    form_dirty(win);
    emit_input(win, node, text, "insertCompositionText");
  }
  emit_composition(win, node, "compositionend", text);
}

void move_caret(Node *node, int target, bool extend) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  size_t length = xtgui::xt_form_utf16_length(state->value);
  if (target < 0) target = 0;
  if ((size_t)target > length) target = (int)length;
  if (!extend) {
    state->selection_start = target;
    state->selection_end = target;
    state->selection_direction = 0;
    return;
  }
  if (target < state->selection_start) {
    state->selection_start = target;
    state->selection_direction = -1;
  } else {
    state->selection_end = target;
    state->selection_direction = 1;
  }
}

bool delete_by(Node *node, int direction) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return false;
  if (selection_length(state) > 0) {
    replace_range(node, state->selection_start, state->selection_end, "");
    return true;
  }
  size_t caret = (size_t)state->selection_start;
  size_t other = direction < 0 ? xtgui::xt_form_utf16_prev(state->value, caret)
                              : xtgui::xt_form_utf16_next(state->value, caret);
  if (other == caret) return false;
  size_t begin = direction < 0 ? other : caret;
  size_t end = direction < 0 ? caret : other;
  replace_range(node, (int)begin, (int)end, "");
  return true;
}

void select_all(Node *node) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  int length = (int)xtgui::xt_form_utf16_length(state->value);
  state->selection_start = 0;
  state->selection_end = length;
  state->selection_direction = 1;
}

void copy_selection(Node *node, bool cut) {
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr || state->selection_end <= state->selection_start) return;
  std::string selected =
      xtgui::xt_form_utf16_slice(state->value, (size_t)state->selection_start,
                                 (size_t)state->selection_end);
  SDL_SetClipboardText(selected.c_str());
  if (cut) replace_range(node, state->selection_start, state->selection_end, "");
}

void paste_clipboard(XtGuiWindow *win, Node *node) {
  char *text = SDL_GetClipboardText();
  if (text == nullptr) return;
  FormState *state = xtgui::xt_form_state(node);
  if (state != nullptr) {
    replace_range(node, state->selection_start, state->selection_end, text);
    form_dirty(win);
    emit_input(win, node, text, "insertFromPaste");
  }
  SDL_free(text);
}

int caret_at_x(XtGuiWindow *win, Node *node, float x) {
  if (win == nullptr || win->document == nullptr) return 0;
  xt_gui_flush_dom(win);
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  const xtgui::XtStyle *style = win->document->styleOf(node);
  if (box == nullptr || style == nullptr) return 0;
  float local = x + win->scroll_x - box->content_x;
  if (local <= 0.0f) return 0;
  const FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return 0;
  std::string display = state->value;
  if (xtgui::xt_form_type(node) == "password") display = xtgui::xt_form_mask(display);
  size_t length = xtgui::xt_form_utf16_length(state->value);
  size_t best = 0;
  float best_distance = std::fabs(local);
  for (size_t index = 0; index <= length; index++) {
    std::string prefix = xtgui::xt_form_utf16_slice(display, 0, index);
    float width = xtgui::xt_layout_text_width(prefix, *style);
    float distance = std::fabs(local - width);
    if (distance < best_distance) {
      best_distance = distance;
      best = index;
    }
  }
  return (int)best;
}

void set_range_value(XtGuiWindow *win, Node *node, float x) {
  if (win == nullptr || win->document == nullptr) return;
  xt_gui_flush_dom(win);
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  if (box == nullptr || box->content_width <= 0.0f) return;
  float fraction = (x + win->scroll_x - box->content_x) / box->content_width;
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  auto number_attr = [node](const char *name, double fallback) {
    const std::string *value = node->attr(name);
    if (value == nullptr) return fallback;
    return std::strtod(value->c_str(), nullptr);
  };
  double minimum = number_attr("min", 0.0);
  double maximum = number_attr("max", 100.0);
  double step = number_attr("step", 1.0);
  if (maximum <= minimum) maximum = minimum + 1.0;
  double value = minimum + fraction * (maximum - minimum);
  if (step > 0.0) value = minimum + std::floor((value - minimum) / step + 0.5) * step;
  char buffer[64];
  if (value == std::floor(value)) {
    std::snprintf(buffer, sizeof(buffer), "%d", (int)value);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  }
  xtgui::xt_form_set_value(node, buffer);
  form_dirty(win);
}

void activate_control(XtGuiWindow *win, Node *node) {
  if (node == nullptr || xtgui::xt_form_is_disabled(node)) return;
  if (node->isElement() && node->tag == "label") {
    Node *target = nullptr;
    const std::string *for_attr = node->attr("for");
    if (for_attr != nullptr) {
      const Node *root = node;
      while (root->parent != nullptr) root = root->parent;
      struct FindById {
        const std::string &id;
        const Node *operator()(const Node *current) const {
          if (current->isElement()) {
            const std::string *value = current->attr("id");
            if (value != nullptr && *value == id) return current;
          }
          for (const std::unique_ptr<Node> &child : current->children) {
            if (const Node *found = (*this)(child.get())) return found;
          }
          return nullptr;
        }
      } find = {*for_attr};
      target = const_cast<Node *>(find(root));
    }
    if (target == nullptr) {
      struct FindControl {
        const Node *operator()(const Node *current) const {
          if (xtgui::xt_form_is_focusable(current)) return current;
          for (const std::unique_ptr<Node> &child : current->children) {
            if (const Node *found = (*this)(child.get())) return found;
          }
          return nullptr;
        }
      } find_control;
      target = const_cast<Node *>(find_control(node));
    }
    if (target != nullptr && target != node) {
      xt_gui_set_focus(win, target);
      activate_control(win, target);
    }
    return;
  }
  if (xtgui::xt_form_is_checkable(node)) {
    if (xtgui::xt_form_type(node) == "radio") {
      xtgui::xt_form_set_checked(node, true);
    } else {
      xtgui::xt_form_set_checked(node, !xtgui::xt_form_checked(node));
    }
    form_dirty(win);
    return;
  }
  if (xtgui::xt_form_is_select(node)) {
    FormState *state = xtgui::xt_form_state(node);
    if (state == nullptr) return;
    int count = 0;
    struct Count {
      int *count;
      void visit(const Node *current) const {
        if (current->isElement() && current->isTag("option")) (*count)++;
        for (const std::unique_ptr<Node> &child : current->children) visit(child.get());
      }
    } counter = {&count};
    counter.visit(node);
    if (count > 0) {
      state->selected_index = (state->selected_index + 1) % count;
      const Node *option = xtgui::xt_form_selected_option(node);
      state->value = option != nullptr ? xtgui::xt_form_option_value(option) : "";
      form_dirty(win);
    }
  }
}

}  // namespace

bool xt_gui_set_focus(XtGuiWindow *win, xtgui::Node *node) {
  if (win == nullptr || win->document == nullptr) return false;
  Node *old = const_cast<Node *>(win->document->focus());
  if (old == node) return false;
  if (old != nullptr) {
    FormState *old_state = xtgui::xt_form_state(old);
    if (old_state != nullptr) old_state->focused = false;
  }
  bool changed = win->document->setFocus(node);
  if (node != nullptr) {
    FormState *new_state = xtgui::xt_form_state(node);
    if (new_state != nullptr) new_state->focused = true;
  }
  if (win->window != nullptr) {
    if (form_editable(node)) {
      SDL_StartTextInput(win->window);
    } else {
      SDL_StopTextInput(win->window);
    }
  }
  /* The caret, focus ring and `:focus` style all change even when the sheet has
   * no `:focus` rule (`XtDocument::setFocus` then returns false). */
  form_dirty(win);
  win->present_dirty = 1;
  return changed || node != nullptr;
}

void xt_gui_form_focus_at(XtGuiWindow *win, xtgui::Node *node, float x) {
  if (win == nullptr || node == nullptr) return;
  xt_gui_set_focus(win, node);
  if (!form_editable(node)) return;
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  int caret = caret_at_x(win, node, x);
  state->selection_start = caret;
  state->selection_end = caret;
  state->selection_direction = 0;
  form_dirty(win);
}

void xt_gui_form_pointer(XtGuiWindow *win, const char *type, xtgui::Node *node, float x, float y) {
  (void)y;
  if (win == nullptr || node == nullptr || !node->isElement()) return;
  std::string event = type;
  if (event == "mousedown") {
    if (xtgui::xt_form_is_focusable(node)) xt_gui_form_focus_at(win, node, x);
    if (xtgui::xt_form_is_input(node) && xtgui::xt_form_type(node) == "range") {
      FormState *state = xtgui::xt_form_state(node);
      if (state != nullptr) state->dragging = true;
      set_range_value(win, node, x);
    }
    return;
  }
  if (event == "mousemove") {
    FormState *state = xtgui::xt_form_state(node);
    if (state != nullptr && state->dragging &&
        xtgui::xt_form_is_input(node) && xtgui::xt_form_type(node) == "range") {
      set_range_value(win, node, x);
    }
    return;
  }
  if (event == "mouseup") {
    FormState *state = xtgui::xt_form_state(node);
    if (state != nullptr) state->dragging = false;
    return;
  }
  if (event == "click") activate_control(win, node);
}

void xt_gui_form_activate(XtGuiWindow *win, xtgui::Node *node) { activate_control(win, node); }

namespace {

struct TabStop {
  Node *node;
  int index;
  int order;
};

int tab_index_of(const Node *node) {
  const std::string *value = node->attr("tabindex");
  if (value != nullptr) return (int)std::strtol(value->c_str(), nullptr, 10);
  return 0;
}

void collect_tab_stops(const Node *node, std::vector<TabStop> *out, int *order) {
  if (node->isElement() && xtgui::xt_form_is_focusable(node)) {
    int index = tab_index_of(node);
    if (index >= 0) out->push_back({const_cast<Node *>(node), index, (*order)++});
  }
  for (const std::unique_ptr<Node> &child : node->children) {
    collect_tab_stops(child.get(), out, order);
  }
}

void ensure_visible(XtGuiWindow *win, Node *node) {
  if (win == nullptr || win->document == nullptr) return;
  xt_gui_flush_dom(win);
  const xtgui::LayoutBox *box = win->document->boxOf(node);
  if (box == nullptr) return;
  float viewport_width = 0;
  float viewport_height = 0;
  xt_gui_window_viewport(win, &viewport_width, &viewport_height);
  float x = win->scroll_x;
  float y = win->scroll_y;
  if (box->x < x) x = box->x;
  else if (box->x + box->width > x + viewport_width) x = box->x + box->width - viewport_width;
  if (box->y < y) y = box->y;
  else if (box->y + box->height > y + viewport_height) y = box->y + box->height - viewport_height;
  xt_gui_scroll_to(win, x, y);
}

}  // namespace

void xt_gui_focus_step(XtGuiWindow *win, bool forward) {
  if (win == nullptr || win->document == nullptr) return;
  std::vector<TabStop> stops;
  int order = 0;
  collect_tab_stops(win->document->root(), &stops, &order);
  if (stops.empty()) {
    xt_gui_set_focus(win, nullptr);
    return;
  }
  std::stable_sort(stops.begin(), stops.end(), [](const TabStop &a, const TabStop &b) {
    if (a.index == 0 && b.index == 0) return a.order < b.order;
    if (a.index == 0) return false;
    if (b.index == 0) return true;
    if (a.index != b.index) return a.index < b.index;
    return a.order < b.order;
  });
  int current = -1;
  Node *focused = const_cast<Node *>(win->document->focus());
  for (size_t i = 0; i < stops.size(); i++) {
    if (stops[i].node == focused) {
      current = (int)i;
      break;
    }
  }
  int next;
  if (current < 0) {
    next = forward ? 0 : (int)stops.size() - 1;
  } else {
    next = (int)((size_t)current + (forward ? 1u : (size_t)stops.size() - 1u)) % (int)stops.size();
  }
  xt_gui_set_focus(win, stops[(size_t)next].node);
  ensure_visible(win, stops[(size_t)next].node);
}

void xt_gui_text_input(XtGuiWindow *win, const char *text) {
  Node *node = nullptr;
  FormState *state = focused_state(win, &node);
  if (state == nullptr || text == nullptr) return;
  std::string produced(text);
  if (produced.empty()) return;
  if (state->composing) {
    commit_composition(win, node, produced);
    form_dirty(win);
    return;
  }
  if (emit_keypress(win, node, produced)) return;
  if (produced == "\n" || produced == "\r" || produced == "\r\n") {
    if (!xtgui::xt_form_is_textarea(node)) return;
    produced = "\n";
  }
  int start = state->selection_start;
  int end = state->selection_end;
  replace_range(node, start, end, produced);
  form_dirty(win);
  emit_input(win, node, produced, "insertText");
}

void xt_gui_text_editing(XtGuiWindow *win, const char *text, int start, int length) {
  (void)start;
  (void)length;
  Node *node = nullptr;
  FormState *state = focused_state(win, &node);
  if (state == nullptr || text == nullptr) return;
  if (!state->composing) {
    state->composing = true;
    state->composition_start = state->selection_start;
    state->composition_end = state->selection_end;
    emit_composition(win, node, "compositionstart", text);
  }
  state->composition = text;
  emit_composition(win, node, "compositionupdate", text);
  form_dirty(win);
}

void xt_gui_composition_event(XtGuiWindow *win, const char *type, const char *data) {
  Node *node = nullptr;
  FormState *state = focused_state(win, &node);
  if (state == nullptr) return;
  std::string name = type != nullptr ? type : "";
  std::string text = data != nullptr ? data : "";
  if (name == "compositionstart") {
    if (!state->composing) {
      state->composing = true;
      state->composition_start = state->selection_start;
      state->composition_end = state->selection_end;
    }
    state->composition = text;
    emit_composition(win, node, "compositionstart", text);
    form_dirty(win);
    return;
  }
  if (name == "compositionupdate") {
    if (!state->composing) return;
    state->composition = text;
    emit_composition(win, node, "compositionupdate", text);
    form_dirty(win);
    return;
  }
  if (name == "compositionend") {
    if (text.empty()) text = state->composition;
    commit_composition(win, node, text);
    form_dirty(win);
  }
}

void xt_gui_form_key_down(XtGuiWindow *win, xt_value payload) {
  Node *node = nullptr;
  FormState *state = focused_state(win, &node);
  if (state == nullptr) return;
  auto field = [payload](const char *name) { return xt_object_get_cstr(payload, name); };
  auto text_field = [&field](const char *name) {
    xt_value value = field(name);
    if (!XT_IS_STRING(value)) return std::string();
    const char *data = xt_string_data(value);
    return data != nullptr ? std::string(data) : std::string();
  };
  std::string key = text_field("key");
  if (key.empty()) return;
  bool ctrl = xt_truthy(field("ctrlKey")) || xt_truthy(field("ctrl"));
  bool meta = xt_truthy(field("metaKey")) || xt_truthy(field("meta"));
  bool shift = xt_truthy(field("shiftKey")) || xt_truthy(field("shift"));
  bool alt = xt_truthy(field("altKey")) || xt_truthy(field("alt"));
  bool physical = xt_truthy(field("__xt_gui_physical"));

  if (ctrl || meta) {
    std::string lower = key;
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower == "a") {
      select_all(node);
      form_dirty(win);
      return;
    }
    if (lower == "c") {
      copy_selection(node, false);
      return;
    }
    if (lower == "x") {
      copy_selection(node, true);
      form_dirty(win);
      emit_input(win, node, "", "deleteByCut");
      return;
    }
    if (lower == "v") {
      paste_clipboard(win, node);
      return;
    }
    return;
  }

  int caret = state->selection_start;
  int end = state->selection_end;
  if (key == "Backspace") {
    if (delete_by(node, -1)) {
      form_dirty(win);
      emit_input(win, node, "", "deleteContentBackward");
    }
    return;
  }
  if (key == "Delete") {
    if (delete_by(node, 1)) {
      form_dirty(win);
      emit_input(win, node, "", "deleteContentForward");
    }
    return;
  }
  if (key == "ArrowLeft") {
    size_t target = selection_length(state) > 0 && !shift
                        ? (size_t)state->selection_start
                        : xtgui::xt_form_utf16_prev(state->value, (size_t)caret);
    move_caret(node, (int)target, shift);
    form_dirty(win);
    return;
  }
  if (key == "ArrowRight") {
    size_t target = selection_length(state) > 0 && !shift
                        ? (size_t)state->selection_end
                        : xtgui::xt_form_utf16_next(state->value, (size_t)end);
    move_caret(node, (int)target, shift);
    form_dirty(win);
    return;
  }
  if (key == "Home" || key == "End") {
    int target = key == "Home" ? 0 : (int)xtgui::xt_form_utf16_length(state->value);
    move_caret(node, target, shift);
    form_dirty(win);
    return;
  }
  if (key == "Enter") {
    if (xtgui::xt_form_is_textarea(node)) {
      if (emit_keypress(win, node, "\n")) return;
      replace_range(node, caret, end, "\n");
      form_dirty(win);
      emit_input(win, node, "\n", "insertLineBreak");
    }
    return;
  }
  if (key == "Tab") return;  /* Tab order is handled by the focus pass. */

  if (physical) return;  /* SDL sends the produced text separately. */
  std::string produced = text_field("text");
  if (produced.empty() && !alt) {
    size_t units = xtgui::xt_form_utf16_length(key);
    if (units >= 1 && key != "Escape" && key != "ArrowUp" && key != "ArrowDown") produced = key;
  }
  if (produced.empty()) return;
  if (emit_keypress(win, node, produced)) return;
  replace_range(node, caret, end, produced);
  form_dirty(win);
  emit_input(win, node, produced, "insertText");
}
