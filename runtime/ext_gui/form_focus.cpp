/*
 * xbintsc GUI engine — pointer activation, tab order and form submit/reset.
 *
 * Text editing lives in `form_input.cpp`; this unit owns what a click or a Tab
 * does: focus placement, checkbox/radio/select activation, the tabbable order,
 * and the `submit`/`reset` events of a `<form>`.
 */

#include "gui_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "form_internal.h"
#include "form_state.h"
#include "layout.h"

namespace {

using xtgui::FormState;
using xtgui::Node;

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
  xtgui::formdetail::dirty(win);
  xtgui::formdetail::emit_input(win, node, "", "insertReplacementText");
}

bool reset_form(XtGuiWindow *win, Node *form) {
  if (form == nullptr || !form->isElement() || form->tag != "form") return false;
  xt_value payload = xtgui::formdetail::make_payload("reset");
  if (xt_gui_emit_dom_event(win, "reset", form, payload)) return false;
  xtgui::formdetail::reset_controls(win, form);
  xtgui::formdetail::restyle(win);
  return true;
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
  if (xtgui::xt_form_is_button_like(node)) {
    std::string type = xtgui::xt_form_type(node);
    if (type == "reset") {
      reset_form(win, xtgui::formdetail::form_ancestor(node));
    } else if (type == "submit" || type == "image") {
      xtgui::formdetail::submit_form(win, xtgui::formdetail::form_ancestor(node), node);
    }
    return;
  }
  if (node->isElement() && node->tag == "button") {
    const std::string *attr = node->attr("type");
    std::string type = attr != nullptr ? *attr : "submit";
    for (char &c : type) c = (char)std::tolower((unsigned char)c);
    if (type == "reset") {
      reset_form(win, xtgui::formdetail::form_ancestor(node));
    } else if (type != "button") {
      xtgui::formdetail::submit_form(win, xtgui::formdetail::form_ancestor(node), node);
    }
    return;
  }
  if (xtgui::xt_form_is_checkable(node)) {
    if (xtgui::xt_form_type(node) == "radio") {
      xtgui::xt_form_set_checked(node, true);
    } else {
      xtgui::xt_form_set_checked(node, !xtgui::xt_form_checked(node));
    }
    xtgui::formdetail::restyle(win);
    xtgui::formdetail::emit_input(win, node, "", "insertReplacementText");
    xtgui::formdetail::emit_change(win, node);
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
      xtgui::formdetail::restyle(win);
      xtgui::formdetail::emit_input(win, node, "", "insertReplacementText");
      xtgui::formdetail::emit_change(win, node);
    }
  }
}

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

void xt_gui_form_focus_at(XtGuiWindow *win, xtgui::Node *node, float x) {
  if (win == nullptr || node == nullptr) return;
  xt_gui_set_focus(win, node);
  if (!xtgui::formdetail::editable(node)) return;
  FormState *state = xtgui::xt_form_state(node);
  if (state == nullptr) return;
  int caret = xtgui::formdetail::caret_at_x(win, node, x);
  state->selection_start = caret;
  state->selection_end = caret;
  state->selection_direction = 0;
  xtgui::formdetail::dirty(win);
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
    if (state != nullptr && state->dragging && xtgui::xt_form_is_input(node) &&
        xtgui::xt_form_type(node) == "range") {
      set_range_value(win, node, x);
    }
    return;
  }
  if (event == "mouseup") {
    FormState *state = xtgui::xt_form_state(node);
    if (state != nullptr) {
      bool was_dragging = state->dragging;
      state->dragging = false;
      if (was_dragging && xtgui::xt_form_is_input(node) &&
          xtgui::xt_form_type(node) == "range") {
        xtgui::formdetail::emit_change(win, node);
      }
    }
    return;
  }
  if (event == "click") activate_control(win, node);
}

void xt_gui_form_activate(XtGuiWindow *win, xtgui::Node *node) { activate_control(win, node); }

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

void xt_gui_form_submit_request(XtGuiWindow *win, xtgui::Node *form, xtgui::Node *submitter) {
  xtgui::formdetail::submit_form(win, form, submitter);
}

void xt_gui_form_reset(XtGuiWindow *win, xtgui::Node *form) { reset_form(win, form); }
