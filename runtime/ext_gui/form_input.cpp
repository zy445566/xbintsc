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
#include "form_internal.h"
#include "layout.h"

namespace {

using xtgui::FormState;
using xtgui::Node;

FormState *focused_state(XtGuiWindow *win, Node **node_out) {
  if (node_out != nullptr) *node_out = nullptr;
  if (win == nullptr || win->document == nullptr) return nullptr;
  Node *node = const_cast<Node *>(win->document->focus());
  if (!xtgui::formdetail::editable(node)) return nullptr;
  if (node_out != nullptr) *node_out = node;
  return xtgui::xt_form_state(node);
}


void emit_composition(XtGuiWindow *win, Node *node, const char *type, const std::string &data) {
  xt_value payload = xtgui::formdetail::make_payload(type);
  xt_object_set(payload, xt_string_from_cstr("data"), xt_string_from_cstr(data.c_str()));
  xt_object_set(payload, xt_string_from_cstr("isComposing"),
                std::string(type) == "compositionend" ? XT_FALSE : XT_TRUE);
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xtgui::xt_form_value(node).c_str()));
  xt_gui_emit_dom_event(win, type, node, payload);
}

bool before_input(XtGuiWindow *win, Node *node, const std::string &data, const char *input_type) {
  xt_value payload = xtgui::formdetail::make_payload("beforeinput");
  xt_object_set(payload, xt_string_from_cstr("data"), xt_string_from_cstr(data.c_str()));
  xt_object_set(payload, xt_string_from_cstr("inputType"), xt_string_from_cstr(input_type));
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xtgui::xt_form_value(node).c_str()));
  return xt_gui_emit_dom_event(win, "beforeinput", node, payload);
}

/** Emit `keypress`; true when a listener cancelled the insertion. */
bool emit_keypress(XtGuiWindow *win, Node *node, const std::string &text) {
  if (text.empty()) return false;
  xt_value payload = xtgui::formdetail::make_payload("keypress");
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
  if (!text.empty() && !before_input(win, node, text, "insertCompositionText")) {
    replace_range(node, start, end, text);
    xtgui::formdetail::dirty(win);
    xtgui::formdetail::emit_input(win, node, text, "insertCompositionText");
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
  if (state != nullptr && !before_input(win, node, text, "insertFromPaste")) {
    replace_range(node, state->selection_start, state->selection_end, text);
    xtgui::formdetail::dirty(win);
    xtgui::formdetail::emit_input(win, node, text, "insertFromPaste");
  }
  SDL_free(text);
}

}  // namespace
bool xt_gui_set_focus(XtGuiWindow *win, xtgui::Node *node) {
  if (win == nullptr || win->document == nullptr) return false;
  Node *old = const_cast<Node *>(win->document->focus());
  if (old == node) return false;
  if (old != nullptr) {
    FormState *old_state = xtgui::xt_form_state(old);
    if (old_state != nullptr) {
      /* `change` for a text field fires when focus leaves and the value differs
       * from the value it had when focus arrived (the browser's rule). */
      if (xtgui::formdetail::editable(old) && old_state->focus_value_valid &&
          old_state->value != old_state->focus_value) {
        xtgui::formdetail::emit_change(win, old);
      }
      old_state->focus_value_valid = false;
      old_state->focused = false;
    }
  }
  bool changed = win->document->setFocus(node);
  if (node != nullptr) {
    FormState *new_state = xtgui::xt_form_state(node);
    if (new_state != nullptr) {
      new_state->focused = true;
      if (xtgui::formdetail::editable(node)) {
        new_state->focus_value = new_state->value;
        new_state->focus_value_valid = true;
      }
    }
  }
  if (win->window != nullptr) {
    if (xtgui::formdetail::editable(node)) {
      SDL_StartTextInput(win->window);
    } else {
      SDL_StopTextInput(win->window);
    }
  }
  /* The caret, focus ring and `:focus` style all change even when the sheet has
   * no `:focus` rule (`XtDocument::setFocus` then returns false). */
  xtgui::formdetail::dirty(win);
  win->present_dirty = 1;
  return changed || node != nullptr;
}

void xt_gui_text_input(XtGuiWindow *win, const char *text) {
  Node *node = nullptr;
  FormState *state = focused_state(win, &node);
  if (state == nullptr || text == nullptr) return;
  std::string produced(text);
  if (produced.empty()) return;
  if (state->composing) {
    commit_composition(win, node, produced);
    xtgui::formdetail::dirty(win);
    return;
  }
  if (emit_keypress(win, node, produced)) return;
  if (produced == "\n" || produced == "\r" || produced == "\r\n") {
    if (!xtgui::xt_form_is_textarea(node)) return;
    produced = "\n";
  }
  if (before_input(win, node, produced, "insertText")) return;
  int start = state->selection_start;
  int end = state->selection_end;
  replace_range(node, start, end, produced);
  xtgui::formdetail::dirty(win);
  xtgui::formdetail::emit_input(win, node, produced, "insertText");
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
  xtgui::formdetail::dirty(win);
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
    xtgui::formdetail::dirty(win);
    return;
  }
  if (name == "compositionupdate") {
    if (!state->composing) return;
    state->composition = text;
    emit_composition(win, node, "compositionupdate", text);
    xtgui::formdetail::dirty(win);
    return;
  }
  if (name == "compositionend") {
    if (text.empty()) text = state->composition;
    commit_composition(win, node, text);
    xtgui::formdetail::dirty(win);
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
      xtgui::formdetail::dirty(win);
      return;
    }
    if (lower == "c") {
      copy_selection(node, false);
      return;
    }
    if (lower == "x") {
      if (before_input(win, node, "", "deleteByCut")) return;
      copy_selection(node, true);
      xtgui::formdetail::dirty(win);
      xtgui::formdetail::emit_input(win, node, "", "deleteByCut");
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
    if (before_input(win, node, "", "deleteContentBackward")) return;
    if (delete_by(node, -1)) {
      xtgui::formdetail::dirty(win);
      xtgui::formdetail::emit_input(win, node, "", "deleteContentBackward");
    }
    return;
  }
  if (key == "Delete") {
    if (before_input(win, node, "", "deleteContentForward")) return;
    if (delete_by(node, 1)) {
      xtgui::formdetail::dirty(win);
      xtgui::formdetail::emit_input(win, node, "", "deleteContentForward");
    }
    return;
  }
  if (key == "ArrowLeft") {
    size_t target = selection_length(state) > 0 && !shift
                        ? (size_t)state->selection_start
                        : xtgui::xt_form_utf16_prev(state->value, (size_t)caret);
    move_caret(node, (int)target, shift);
    xtgui::formdetail::dirty(win);
    return;
  }
  if (key == "ArrowRight") {
    size_t target = selection_length(state) > 0 && !shift
                        ? (size_t)state->selection_end
                        : xtgui::xt_form_utf16_next(state->value, (size_t)end);
    move_caret(node, (int)target, shift);
    xtgui::formdetail::dirty(win);
    return;
  }
  if (key == "Home" || key == "End") {
    int target = key == "Home" ? 0 : (int)xtgui::xt_form_utf16_length(state->value);
    move_caret(node, target, shift);
    xtgui::formdetail::dirty(win);
    return;
  }
  if (key == "Enter") {
    if (xtgui::xt_form_is_textarea(node)) {
      if (emit_keypress(win, node, "\n")) return;
      if (before_input(win, node, "\n", "insertLineBreak")) return;
      replace_range(node, caret, end, "\n");
      xtgui::formdetail::dirty(win);
      xtgui::formdetail::emit_input(win, node, "\n", "insertLineBreak");
    } else {
      /* Enter in a single-line field commits the value and submits its form. */
      if (state->focus_value_valid && state->value != state->focus_value) {
        xtgui::formdetail::emit_change(win, node);
        state->focus_value = state->value;
      }
      Node *form = xtgui::formdetail::form_ancestor(node);
      if (form != nullptr) xtgui::formdetail::submit_form(win, form, nullptr);
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
  if (before_input(win, node, produced, "insertText")) return;
  replace_range(node, caret, end, produced);
  xtgui::formdetail::dirty(win);
  xtgui::formdetail::emit_input(win, node, produced, "insertText");
}
