/*
 * xbintsc GUI engine — form-control classification, value state and the
 * UTF-8/UTF-16 conversions the DOM selection API needs.
 *
 * A form control's live value lives in `Node::form` (`FormState`). These
 * helpers are shared by the DOM bindings (`dom_api_form.cpp`), input dispatch
 * (`form_input.cpp`), layout (`layout_flow.cpp`) and paint (`paint_form.cpp`),
 * so "what kind of control is this" and "which bytes sit at UTF-16 offset N"
 * are answered one way everywhere.
 */
#ifndef XT_GUI_FORM_STATE_H
#define XT_GUI_FORM_STATE_H

#include <cstddef>
#include <string>

#include "dom.h"

namespace xtgui {

/* -- classification ------------------------------------------------------- */

bool xt_form_is_input(const Node *node);
bool xt_form_is_textarea(const Node *node);
bool xt_form_is_select(const Node *node);
/** `<input>`, `<textarea>` or `<select>`. */
bool xt_form_is_control(const Node *node);
/** The input type, lowercased and normalised (`"text"` when absent/unknown).
 * `textarea`/`select` answer their tag names. */
std::string xt_form_type(const Node *node);
/** Single- or multi-line editable text (`input` text family or `textarea`). */
bool xt_form_is_text_like(const Node *node);
/** `input type=button|submit|reset|image`. */
bool xt_form_is_button_like(const Node *node);
/** `checkbox` or `radio`. */
bool xt_form_is_checkable(const Node *node);
bool xt_form_is_disabled(const Node *node);
bool xt_form_is_readonly(const Node *node);
/** Whether the element can receive focus (and therefore keyboard input). */
bool xt_form_is_focusable(const Node *node);

/* -- state ---------------------------------------------------------------- */

/** The control's state, creating and initialising it on first use. */
FormState *xt_form_state(Node *node);
const FormState *xt_form_state(const Node *node);
/** Read the initial value from the `value` attribute / `<textarea>` text /
 * selected `<option>`. A no-op once initialised. */
void xt_form_init(Node *node);
std::string xt_form_value(const Node *node);
void xt_form_set_value(Node *node, const std::string &value);
bool xt_form_checked(const Node *node);
void xt_form_set_checked(Node *node, bool checked);
/** Uncheck every other radio in the same group (same `name`). Returns true when
 * something changed. */
bool xt_form_radio_group_set(Node *node);

/* -- button/option labels ------------------------------------------------- */

/** The label an `<input type=button|submit|reset>` paints: its `value` or the
 * UA default (`Submit`, `Reset`, `Button`). */
std::string xt_form_button_label(const Node *node);
/** An `<option>`'s submitted value (`value` attribute, else its text). */
std::string xt_form_option_value(const Node *option);
/** An `<option>`'s visible text (whitespace-collapsed). */
std::string xt_form_option_label(const Node *option);
/** The currently selected `<option>` of a `<select>`, or null. */
Node *xt_form_selected_option(Node *select);
const Node *xt_form_selected_option(const Node *select);

/* -- intrinsic size ------------------------------------------------------- */

/** The content-box size a control gets when `width`/`height` are `auto`.
 * `style` is the control's computed style (needed to measure its text). */
void xt_form_intrinsic_size(const Node *node, const struct XtStyle &style, float *width,
                            float *height);

/* -- UTF-8 / UTF-16 ------------------------------------------------------- */

/** JavaScript string length: UTF-16 code units. */
size_t xt_form_utf16_length(const std::string &text);
/** Byte offset of UTF-16 code-unit index `units` (clamped to the text end). */
size_t xt_form_utf16_byte(const std::string &text, size_t units);
/** UTF-16 index of byte offset `bytes` (clamped to the text end). */
size_t xt_form_utf16_index(const std::string &text, size_t bytes);
/** `text.slice(startUnits, endUnits)` as UTF-8 bytes. */
std::string xt_form_utf16_slice(const std::string &text, size_t start_units, size_t end_units);
/** Step one code point left/right in UTF-16 units. */
size_t xt_form_utf16_prev(const std::string &text, size_t units);
size_t xt_form_utf16_next(const std::string &text, size_t units);

/** Replace every code point with U+2022 (password field masking). */
std::string xt_form_mask(const std::string &text);

}  // namespace xtgui

#endif /* XT_GUI_FORM_STATE_H */
