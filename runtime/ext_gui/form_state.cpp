/*
 * xbintsc GUI engine — form-control state helpers.
 *
 * The engine is byte-oriented (UTF-8) while the DOM selection API is
 * UTF-16-code-unit-oriented, so the conversions at the bottom are the single
 * place that maps between the two. Everything else classifies an element,
 * initialises its live value from the authored markup, or measures the
 * intrinsic size a control gets when CSS leaves it `auto`.
 */

#include "form_state.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "layout.h"

namespace xtgui {

namespace {

std::string lowerAscii(const std::string &text) {
  std::string out = text;
  for (char &c : out) c = (char)std::tolower((unsigned char)c);
  return out;
}

std::string trimAscii(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (unsigned char)text[begin] <= ' ') begin++;
  while (end > begin && (unsigned char)text[end - 1] <= ' ') end--;
  return text.substr(begin, end - begin);
}

/** Decode one UTF-8/WTF-8 code point; invalid bytes are taken verbatim. */
uint32_t decodeCodePoint(const std::string &text, size_t offset, size_t *width) {
  const unsigned char *s = (const unsigned char *)text.data();
  size_t length = text.size();
  unsigned char c = s[offset];
  if (c < 0x80) {
    *width = 1;
    return c;
  }
  if ((c & 0xE0) == 0xC0 && offset + 1 < length) {
    *width = 2;
    return ((uint32_t)(c & 0x1F) << 6) | (s[offset + 1] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0 && offset + 2 < length) {
    *width = 3;
    return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[offset + 1] & 0x3F) << 6) |
           (s[offset + 2] & 0x3F);
  }
  if ((c & 0xF8) == 0xF0 && offset + 3 < length) {
    *width = 4;
    return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[offset + 1] & 0x3F) << 12) |
           ((uint32_t)(s[offset + 2] & 0x3F) << 6) | (s[offset + 3] & 0x3F);
  }
  *width = 1;
  return c;
}

int codePointUnits(uint32_t code) { return code >= 0x10000 ? 2 : 1; }

void collectOptions(const Node *node, std::vector<const Node *> *out) {
  if (node->isElement() && node->isTag("option")) out->push_back(node);
  for (const std::unique_ptr<Node> &child : node->children) collectOptions(child.get(), out);
}

void collectText(const Node *node, std::string *out) {
  if (node->isText()) {
    *out += node->text;
    return;
  }
  for (const std::unique_ptr<Node> &child : node->children) collectText(child.get(), out);
}

std::string collapseWhitespace(const std::string &text) {
  std::string out;
  bool pending_space = false;
  for (char c : text) {
    if ((unsigned char)c <= ' ') {
      pending_space = !out.empty();
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(c);
  }
  return out;
}

/** Parse the integer value of a `size`/`cols`/`rows` attribute. */
int intAttribute(const Node *node, const char *name, int fallback, int minimum) {
  const std::string *value = node->attr(name);
  if (value == nullptr) return fallback;
  int parsed = (int)std::strtol(value->c_str(), nullptr, 10);
  return parsed >= minimum ? parsed : fallback;
}

const Node *firstOption(const Node *select) {
  if (select == nullptr) return nullptr;
  std::vector<const Node *> options;
  collectOptions(select, &options);
  return options.empty() ? nullptr : options.front();
}

const Node *optionAt(const Node *select, int index) {
  if (select == nullptr || index < 0) return nullptr;
  std::vector<const Node *> options;
  collectOptions(select, &options);
  if ((size_t)index >= options.size()) return nullptr;
  return options[(size_t)index];
}

}  // namespace

/* -- classification ------------------------------------------------------- */

bool xt_form_is_input(const Node *node) {
  return node != nullptr && node->isElement() && node->tag == "input";
}

bool xt_form_is_textarea(const Node *node) {
  return node != nullptr && node->isElement() && node->tag == "textarea";
}

bool xt_form_is_select(const Node *node) {
  return node != nullptr && node->isElement() && node->tag == "select";
}

bool xt_form_is_control(const Node *node) {
  return xt_form_is_input(node) || xt_form_is_textarea(node) || xt_form_is_select(node);
}

std::string xt_form_type(const Node *node) {
  if (xt_form_is_textarea(node)) return "textarea";
  if (xt_form_is_select(node)) return "select-one";
  if (node == nullptr || !node->isElement()) return "text";
  if (node->tag == "button") return "button";
  if (!xt_form_is_input(node)) return "";
  const std::string *attr = node->attr("type");
  std::string type = attr != nullptr ? lowerAscii(trimAscii(*attr)) : "text";
  if (type.empty()) type = "text";
  /* HTML's aliases: `datetime` is `datetime-local`, and an unknown type behaves
   * as `text` (including the reflected `type` property). */
  if (type == "datetime") return "datetime-local";
  static const char *const known[] = {
      "text",    "search", "tel",     "url",      "email",     "password",
      "date",    "month",  "week",    "time",     "datetime-local", "number",
      "range",   "color",  "checkbox", "radio",   "file",      "submit",
      "image",   "reset",  "button",  "hidden",
  };
  for (const char *name : known) {
    if (type == name) return type;
  }
  return "text";
}

bool xt_form_is_text_like(const Node *node) {
  if (!xt_form_is_control(node)) return false;
  if (xt_form_is_textarea(node)) return true;
  std::string type = xt_form_type(node);
  return type == "text" || type == "search" || type == "tel" || type == "url" ||
         type == "email" || type == "password" || type == "number" || type == "date" ||
         type == "month" || type == "week" || type == "time" || type == "datetime-local";
}

bool xt_form_is_button_like(const Node *node) {
  if (!xt_form_is_input(node)) return false;
  std::string type = xt_form_type(node);
  return type == "button" || type == "submit" || type == "reset" || type == "image";
}

bool xt_form_is_checkable(const Node *node) {
  if (!xt_form_is_input(node)) return false;
  std::string type = xt_form_type(node);
  return type == "checkbox" || type == "radio";
}

bool xt_form_is_disabled(const Node *node) {
  return node != nullptr && node->isElement() && node->attr("disabled") != nullptr;
}

bool xt_form_is_readonly(const Node *node) {
  return node != nullptr && node->isElement() && node->attr("readonly") != nullptr;
}

bool xt_form_is_focusable(const Node *node) {
  if (node == nullptr || !node->isElement() || node->is_pseudo) return false;
  if (xt_form_is_disabled(node)) return false;
  if (xt_form_is_control(node)) {
    if (xt_form_is_input(node) && xt_form_type(node) == "hidden") return false;
    return true;
  }
  if (node->tag == "button") return true;
  if (node->tag == "a" && node->attr("href") != nullptr) return true;
  if (node->attr("contenteditable") != nullptr) return true;
  const std::string *tabindex = node->attr("tabindex");
  if (tabindex != nullptr) {
    int value = (int)std::strtol(tabindex->c_str(), nullptr, 10);
    return value >= -1;
  }
  return false;
}

/* -- state ---------------------------------------------------------------- */

std::string xt_form_option_label(const Node *option) {
  if (option == nullptr) return "";
  std::string text;
  collectText(option, &text);
  return collapseWhitespace(text);
}

std::string xt_form_option_value(const Node *option) {
  if (option == nullptr) return "";
  const std::string *value = option->attr("value");
  return value != nullptr ? *value : xt_form_option_label(option);
}

Node *xt_form_selected_option(Node *select) {
  return const_cast<Node *>(xt_form_selected_option(static_cast<const Node *>(select)));
}

const Node *xt_form_selected_option(const Node *select) {
  if (!xt_form_is_select(select)) return nullptr;
  const FormState *state = xt_form_state(select);
  if (state == nullptr) return nullptr;
  const Node *option = optionAt(select, state->selected_index);
  if (option == nullptr) option = firstOption(select);
  return option;
}

FormState *xt_form_state(Node *node) {
  if (!xt_form_is_control(node)) return nullptr;
  if (!node->form) node->form = std::unique_ptr<FormState>(new FormState());
  if (!node->form->initialized) xt_form_init(node);
  return node->form.get();
}

const FormState *xt_form_state(const Node *node) {
  return xt_form_state(const_cast<Node *>(node));
}

void xt_form_init(Node *node) {
  if (!xt_form_is_control(node)) return;
  if (node->form && node->form->initialized) return;
  if (!node->form) node->form = std::unique_ptr<FormState>(new FormState());
  FormState &state = *node->form;
  if (xt_form_is_textarea(node)) {
    std::string text;
    collectText(node, &text);
    state.value = text;
  } else if (xt_form_is_select(node)) {
    std::vector<const Node *> options;
    collectOptions(node, &options);
    int selected = -1;
    for (size_t i = 0; i < options.size(); i++) {
      if (options[i]->attr("selected") != nullptr) {
        selected = (int)i;
        break;
      }
    }
    if (selected < 0 && !options.empty()) selected = 0;
    state.selected_index = selected;
    const Node *option = optionAt(node, selected);
    state.value = option != nullptr ? xt_form_option_value(option) : "";
  } else {
    std::string type = xt_form_type(node);
    if (type == "checkbox" || type == "radio") {
      state.checked = node->attr("checked") != nullptr;
      state.checked_initialized = true;
      const std::string *value = node->attr("value");
      state.value = value != nullptr ? *value : "on";
    } else if (type == "range") {
      const std::string *value = node->attr("value");
      state.value = value != nullptr ? *value : "50";
    } else if (type == "color") {
      const std::string *value = node->attr("value");
      state.value = value != nullptr ? *value : "#000000";
    } else {
      const std::string *value = node->attr("value");
      state.value = value != nullptr ? *value : "";
    }
  }
  state.selection_start = (int)xt_form_utf16_length(state.value);
  state.selection_end = state.selection_start;
  state.initialized = true;
}

std::string xt_form_value(const Node *node) {
  const FormState *state = xt_form_state(node);
  if (state == nullptr) return "";
  if (xt_form_is_select(node)) {
    const Node *option = xt_form_selected_option(node);
    return option != nullptr ? xt_form_option_value(option) : "";
  }
  return state->value;
}

void xt_form_set_value(Node *node, const std::string &value) {
  FormState *state = xt_form_state(node);
  if (state == nullptr) return;
  if (xt_form_is_select(node)) {
    std::vector<const Node *> options;
    collectOptions(node, &options);
    for (size_t i = 0; i < options.size(); i++) {
      if (xt_form_option_value(options[i]) == value) {
        state->selected_index = (int)i;
        state->value = value;
        return;
      }
    }
    state->value = value;
    return;
  }
  state->value = value;
  /* Assigning `value` moves the caret to the end, as browsers do. */
  size_t length = xt_form_utf16_length(value);
  state->selection_start = (int)length;
  state->selection_end = (int)length;
  state->selection_direction = 0;
}

bool xt_form_checked(const Node *node) {
  const FormState *state = xt_form_state(node);
  return state != nullptr && state->checked;
}

void xt_form_set_checked(Node *node, bool checked) {
  FormState *state = xt_form_state(node);
  if (state == nullptr) return;
  state->checked = checked;
  state->checked_initialized = true;
  if (checked && xt_form_type(node) == "radio") xt_form_radio_group_set(node);
}

bool xt_form_radio_group_set(Node *node) {
  if (!xt_form_is_checkable(node) || xt_form_type(node) != "radio") return false;
  const std::string *name = node->attr("name");
  if (name == nullptr || name->empty()) return false;
  const Node *root = node;
  while (root->parent != nullptr) root = root->parent;
  bool changed = false;
  struct Walk {
    const Node *root;
    const Node *self;
    const std::string &name;
    bool *changed;
    void visit(const Node *current) const {
      if (current != self && current->isElement() && current->isTag("input") &&
          xt_form_type(current) == "radio") {
        const std::string *other = current->attr("name");
        if (other != nullptr && *other == name) {
          FormState *state = xt_form_state(const_cast<Node *>(current));
          if (state != nullptr && state->checked) {
            state->checked = false;
            *changed = true;
          }
        }
      }
      for (const std::unique_ptr<Node> &child : current->children) visit(child.get());
    }
  } walk = {root, node, *name, &changed};
  walk.visit(root);
  return changed;
}

std::string xt_form_button_label(const Node *node) {
  if (node == nullptr) return "";
  const std::string *value = node->attr("value");
  if (value != nullptr && !value->empty()) return *value;
  std::string type = xt_form_type(node);
  if (type == "submit") return "Submit";
  if (type == "reset") return "Reset";
  if (type == "image") return "Submit";
  return "Button";
}

/* -- intrinsic size ------------------------------------------------------- */

void xt_form_intrinsic_size(const Node *node, const XtStyle &style, float *width, float *height) {
  *width = 0.0f;
  *height = 0.0f;
  if (!xt_form_is_control(node)) return;
  float line_height = style.font_size * 1.2f;
  float char_width = xt_layout_text_width("0", style);
  if (char_width <= 0.0f) char_width = style.font_size * 0.5f;
  if (xt_form_is_textarea(node)) {
    int cols = intAttribute(node, "cols", 20, 1);
    int rows = intAttribute(node, "rows", 2, 1);
    *width = char_width * (float)cols;
    *height = line_height * (float)rows;
    return;
  }
  if (xt_form_is_select(node)) {
    std::vector<const Node *> options;
    collectOptions(node, &options);
    float widest = 0.0f;
    for (const Node *option : options) {
      widest = std::max(widest, xt_layout_text_width(xt_form_option_label(option), style));
    }
    *width = widest + 22.0f;
    *height = line_height + 4.0f;
    return;
  }
  std::string type = xt_form_type(node);
  if (type == "hidden") return;
  if (type == "checkbox" || type == "radio") {
    *width = 13.0f;
    *height = 13.0f;
    return;
  }
  if (type == "range") {
    *width = 129.0f;
    *height = 20.0f;
    return;
  }
  if (type == "color") {
    *width = 44.0f;
    *height = 20.0f;
    return;
  }
  if (type == "file") {
    *width = xt_layout_text_width("Choose File", style) + 16.0f;
    *height = line_height + 4.0f;
    return;
  }
  if (xt_form_is_button_like(node)) {
    *width = xt_layout_text_width(xt_form_button_label(node), style) + 16.0f;
    *height = line_height + 6.0f;
    return;
  }
  int size = intAttribute(node, "size", 20, 1);
  *width = char_width * (float)size;
  *height = line_height + 2.0f;
}

/* -- UTF-8 / UTF-16 ------------------------------------------------------- */

size_t xt_form_utf16_length(const std::string &text) {
  size_t units = 0;
  size_t offset = 0;
  while (offset < text.size()) {
    size_t width = 0;
    uint32_t code = decodeCodePoint(text, offset, &width);
    units += (size_t)codePointUnits(code);
    offset += width;
  }
  return units;
}

size_t xt_form_utf16_byte(const std::string &text, size_t units) {
  size_t offset = 0;
  size_t current = 0;
  while (offset < text.size()) {
    if (current >= units) return offset;
    size_t width = 0;
    uint32_t code = decodeCodePoint(text, offset, &width);
    int count = codePointUnits(code);
    /* A target inside a surrogate pair snaps to the code point's first byte;
     * UTF-8 cannot represent half of a supplementary character. */
    if (current + (size_t)count > units) return offset;
    current += (size_t)count;
    offset += width;
  }
  return text.size();
}

size_t xt_form_utf16_index(const std::string &text, size_t bytes) {
  size_t offset = 0;
  size_t units = 0;
  while (offset < text.size() && offset < bytes) {
    size_t width = 0;
    uint32_t code = decodeCodePoint(text, offset, &width);
    units += (size_t)codePointUnits(code);
    offset += width;
  }
  return units;
}

std::string xt_form_utf16_slice(const std::string &text, size_t start_units, size_t end_units) {
  size_t begin = xt_form_utf16_byte(text, start_units);
  size_t end = xt_form_utf16_byte(text, end_units);
  if (end <= begin) return "";
  return text.substr(begin, end - begin);
}

size_t xt_form_utf16_prev(const std::string &text, size_t units) {
  if (units == 0) return 0;
  size_t offset = 0;
  size_t current = 0;
  size_t previous = 0;
  while (offset < text.size() && current < units) {
    size_t width = 0;
    uint32_t code = decodeCodePoint(text, offset, &width);
    previous = current;
    current += (size_t)codePointUnits(code);
    offset += width;
  }
  return previous;
}

size_t xt_form_utf16_next(const std::string &text, size_t units) {
  size_t offset = 0;
  size_t current = 0;
  while (offset < text.size()) {
    size_t width = 0;
    uint32_t code = decodeCodePoint(text, offset, &width);
    int count = codePointUnits(code);
    if (current >= units) return current + (size_t)count;
    current += (size_t)count;
    offset += width;
  }
  return current;
}

std::string xt_form_mask(const std::string &text) {
  static const char kBullet[] = "\xE2\x80\xA2";
  std::string out;
  size_t offset = 0;
  while (offset < text.size()) {
    size_t width = 0;
    (void)decodeCodePoint(text, offset, &width);
    out.append(kBullet, 3);
    offset += width;
  }
  return out;
}

}  // namespace xtgui
