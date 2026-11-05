/*
 * xbintsc GUI engine — form helpers shared by text input and activation.
 */

#include "form_internal.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include "form_state.h"
#include "layout.h"

namespace xtgui {
namespace formdetail {

xt_value make_payload(const char *type) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  return payload;
}

void dirty(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->geometry.dirty = 1;
  win->present_dirty = 1;
}

void emit_input(XtGuiWindow *win, Node *node, const std::string &data, const char *input_type) {
  xt_value payload = make_payload("input");
  xt_object_set(payload, xt_string_from_cstr("data"), xt_string_from_cstr(data.c_str()));
  xt_object_set(payload, xt_string_from_cstr("inputType"), xt_string_from_cstr(input_type));
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xt_form_value(node).c_str()));
  xt_object_set(payload, xt_string_from_cstr("isComposing"), XT_FALSE);
  xt_gui_emit_dom_event(win, "input", node, payload);
}

void emit_change(XtGuiWindow *win, Node *node) {
  xt_value payload = make_payload("change");
  xt_object_set(payload, xt_string_from_cstr("value"),
                xt_string_from_cstr(xt_form_value(node).c_str()));
  xt_object_set(payload, xt_string_from_cstr("checked"), xt_form_checked(node) ? XT_TRUE : XT_FALSE);
  xt_gui_emit_dom_event(win, "change", node, payload);
}

bool editable(const Node *node) {
  return node != nullptr && xt_form_is_text_like(node) && !xt_form_is_disabled(node) &&
         !xt_form_is_readonly(node);
}

int caret_at_x(XtGuiWindow *win, Node *node, float x) {
  if (win == nullptr || win->document == nullptr) return 0;
  xt_gui_flush_dom(win);
  const LayoutBox *box = win->document->boxOf(node);
  const XtStyle *style = win->document->styleOf(node);
  if (box == nullptr || style == nullptr) return 0;
  float local = x + win->scroll_x - box->content_x;
  if (local <= 0.0f) return 0;
  const FormState *state = xt_form_state(node);
  if (state == nullptr) return 0;
  std::string display = state->value;
  if (xt_form_type(node) == "password") display = xt_form_mask(display);
  size_t length = xt_form_utf16_length(state->value);
  size_t best = 0;
  float best_distance = std::fabs(local);
  for (size_t index = 0; index <= length; index++) {
    std::string prefix = xt_form_utf16_slice(display, 0, index);
    float width = xt_layout_text_width(prefix, *style);
    float distance = std::fabs(local - width);
    if (distance < best_distance) {
      best_distance = distance;
      best = index;
    }
  }
  return (int)best;
}

Node *form_ancestor(Node *node) {
  for (Node *parent = node != nullptr ? node->parent : nullptr; parent != nullptr;
       parent = parent->parent) {
    if (parent->isElement() && parent->tag == "form") return parent;
  }
  return nullptr;
}

void restyle(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->struct_dirty = 1;
  if (win->document != nullptr) win->document->invalidate();
  win->geometry.dirty = 1;
  win->present_dirty = 1;
}

void reset_controls(XtGuiWindow *win, Node *form) {
  if (form == nullptr) return;
  if (xt_form_is_control(form)) {
    if (form->form) {
      form->form->initialized = false;
      form->form->composing = false;
      form->form->composition.clear();
    }
    xt_form_init(form);
    if (form->form) {
      form->form->selection_start = (int)xt_form_utf16_length(form->form->value);
      form->form->selection_end = form->form->selection_start;
    }
    return;
  }
  for (const std::unique_ptr<Node> &child : form->children) reset_controls(win, child.get());
  if (form->tag == "form") restyle(win);
}

bool submit_form(XtGuiWindow *win, Node *form, Node *submitter) {
  if (form == nullptr || !form->isElement() || form->tag != "form") return false;
  xt_value payload = make_payload("submit");
  if (submitter != nullptr) {
    xt_object_set(payload, xt_string_from_cstr("submitter"), xt_gui_node_handle(win, submitter));
  }
  xt_gui_emit_dom_event(win, "submit", form, payload);
  return true;
}

}  // namespace formdetail
}  // namespace xtgui
