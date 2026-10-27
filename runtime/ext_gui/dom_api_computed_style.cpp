/*
 * xbintsc GUI engine — `getComputedStyle(element, pseudoElt)`.
 *
 * The returned handle reads through the *cascade* (`XtDocument::styleOf`), not
 * the element's inline `style` object, so a rule that comes from a stylesheet or
 * the UA sheet is visible (`getComputedStyle(el).getPropertyValue("display")`).
 * Every read flushes a pending restyle first and resolves the node again, so the
 * handle stays correct after later DOM mutations; generated-content styles are
 * resolved through the current `::before`/`::after` node, not a stale pointer.
 */

#include "dom_api_internal.h"

#include "css.h"
#include "style.h"

namespace domapi {

namespace {

/** Which pseudo-element the handle addresses. `kPseudoUnknown` is a pseudo the
 * engine never generates (`::marker`, `::selection`, …): it keeps answering
 * empty values instead of falling back to the originating element's style. */
enum ComputedPseudo { kPseudoNone = 0, kPseudoBefore = 1, kPseudoAfter = 2, kPseudoUnknown = 3 };

int pseudo_from_argument(xt_value value) {
  if (!XT_IS_STRING(value)) return kPseudoNone;
  std::string text = xtgui::xt_css_lower(xtgui::xt_css_trim(to_string_value(value)));
  if (text.empty()) return kPseudoNone;
  if (text == "::before" || text == ":before") return kPseudoBefore;
  if (text == "::after" || text == ":after") return kPseudoAfter;
  return kPseudoUnknown;
}

/** Resolve the element (and pseudo) the handle currently addresses, applying a
 * pending restyle first so the pseudo arena is part of the same generation. */
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

std::string property_name(int32_t argc, xt_value *argv) {
  if (argc < 1) return std::string();
  return xtgui::xt_css_lower(xtgui::xt_css_trim(to_string_value(xt_arg(argc, argv, 0))));
}

}  // namespace

xt_value computed_style_handle(xt_value element, xt_value pseudo) {
  xt_value handle = xt_object_new_with_proto(computed_style_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_element"), element);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_pseudo"),
                xt_number((double)pseudo_from_argument(pseudo)));
  return handle;
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(computed_style_get_property_value) {
  (void)env;
  const xtgui::XtStyle *style = computed_style_of(self);
  if (style == nullptr) return xt_string_from_cstr("");
  std::string property = property_name(argc, argv);
  if (property.empty()) return xt_string_from_cstr("");
  if (property.size() > 2 && property[0] == '-' && property[1] == '-') {
    auto found = style->variables.find(property);
    return xt_string_from_cstr(found == style->variables.end() ? "" : found->second.c_str());
  }
  return xt_string_from_cstr(xtgui::xt_style_property_to_string(*style, property).c_str());
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

}  // namespace domapi
