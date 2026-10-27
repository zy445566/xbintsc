/*
 * xbintsc GUI engine — `getComputedStyle(element, pseudoElt)`.
 *
 * The handle reads through the *cascade* (`XtDocument::styleOf`), not the
 * element's inline `style` object, and exposes the result both as named
 * properties (`cs.width`, via the runtime's dynamic-property hooks) and through
 * `getPropertyValue`/`getPropertyPriority`. Each read flushes a pending restyle
 * first, so the handle stays correct after later DOM mutations, and
 * `::before`/`::after` resolve the current generated-content node instead of a
 * stale pointer.
 */

#include "dom_api_internal.h"

#include "css.h"
#include "style.h"

namespace domapi {

namespace {

/** Which pseudo-element the handle addresses. `kPseudoUnknown` is a pseudo the
 * engine never generates (`::marker`, `::selection`, …): it keeps answering
 * empty values instead of falling back to the parent element's style. */
enum ComputedPseudo { kPseudoNone = 0, kPseudoBefore = 1, kPseudoAfter = 2, kPseudoUnknown = 3 };

int pseudo_from_argument(xt_value value) {
  if (!XT_IS_STRING(value)) return kPseudoNone;
  std::string text = xtgui::xt_css_lower(xtgui::xt_css_trim(to_string_value(value)));
  if (text.empty()) return kPseudoNone;
  if (text == "::before" || text == ":before") return kPseudoBefore;
  if (text == "::after" || text == ":after") return kPseudoAfter;
  return kPseudoUnknown;
}

const xtgui::XtStyle *computed_style_of(xt_value self) {
  xt_value element = xt_object_get_cstr(self, "__xt_gui_element");
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(element, &win);
  if (node == nullptr || win == nullptr || win->document == nullptr) return nullptr;
  xt_gui_flush_dom(win);
  xt_value sideValue = xt_object_get_cstr(self, "__xt_gui_pseudo");
  int side = XT_IS_NUMBER(sideValue) ? (int)xt_to_number(sideValue) : kPseudoNone;
  if (side == kPseudoUnknown) return nullptr;
  if (side == kPseudoBefore) node = node->pseudo_before;
  else if (side == kPseudoAfter) node = node->pseudo_after;
  if (node == nullptr) return nullptr;
  return win->document->styleOf(node);
}

std::string computed_value(const xtgui::XtStyle *style, const std::string &rawName) {
  if (style == nullptr) return std::string();
  if (rawName.compare(0, 6, "__xt_") == 0) return std::string();
  /* `cs.backgroundColor` and `cs.getPropertyValue("background-color")` name the
   * same longhand; map the camel-cased spelling to the CSS one. */
  std::string mapped;
  for (char c : rawName) {
    if (c >= 'A' && c <= 'Z') {
      mapped.push_back('-');
      mapped.push_back((char)std::tolower((unsigned char)c));
    } else {
      mapped.push_back(c);
    }
  }
  std::string property = xtgui::xt_css_lower(xtgui::xt_css_trim(mapped));
  if (property.empty()) return std::string();
  if (property.size() > 2 && property[0] == '-' && property[1] == '-') {
    auto found = style->variables.find(property);
    return found == style->variables.end() ? std::string() : found->second;
  }
  return xtgui::xt_style_property_to_string(*style, property);
}

xt_value computed_view_get(xt_value self, xt_value key, void *data) {
  (void)data;
  if (!XT_IS_STRING(key)) return XT_UNDEFINED;
  return xt_string_from_cstr(computed_value(computed_style_of(self),
                                            to_string_value(key)).c_str());
}

/** Computed style is read-only; assigning a named property is a no-op. */
int computed_view_set(xt_value self, xt_value key, xt_value value, void *data) {
  (void)self;
  (void)key;
  (void)value;
  (void)data;
  return 1;
}

const xt_object_hooks kComputedStyleHooks = {computed_view_get, computed_view_set, nullptr,
                                             nullptr, nullptr};

xt_value g_computed_proto = XT_UNDEFINED;

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(computed_style_get_property_value) {
  (void)env;
  if (argc < 1) return xt_string_from_cstr("");
  return xt_string_from_cstr(
      computed_value(computed_style_of(self), to_string_value(xt_arg(argc, argv, 0))).c_str());
}

/** Computed styles never carry a declaration priority (spec: always ""). */
METHOD(computed_style_get_property_priority) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_string_from_cstr("");
}

#undef METHOD

}  // namespace

xt_value computed_style_handle(xt_value element, xt_value pseudo) {
  xt_value handle = xt_object_new_with_proto(computed_style_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_element"), element);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_pseudo"),
                xt_number((double)pseudo_from_argument(pseudo)));
  xt_object_set_hooks(handle, &kComputedStyleHooks, nullptr);
  return handle;
}

}  // namespace domapi
