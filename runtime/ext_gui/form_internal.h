/*
 * xbintsc GUI engine — helpers shared by the form input (`form_input.cpp`) and
 * focus/activation (`form_focus.cpp`) units.
 */
#ifndef XT_GUI_FORM_INTERNAL_H
#define XT_GUI_FORM_INTERNAL_H

#include <string>

#include "gui_engine.h"

namespace xtgui {
namespace formdetail {

xt_value make_payload(const char *type);
void dirty(XtGuiWindow *win);
void emit_input(XtGuiWindow *win, Node *node, const std::string &data, const char *input_type);
void emit_change(XtGuiWindow *win, Node *node);
bool editable(const Node *node);
/** UTF-16 caret index nearest to the viewport x coordinate. */
int caret_at_x(XtGuiWindow *win, Node *node, float x);
Node *form_ancestor(Node *node);
void restyle(XtGuiWindow *win);
void reset_controls(XtGuiWindow *win, Node *form);
bool submit_form(XtGuiWindow *win, Node *form, Node *submitter);

}  // namespace formdetail
}  // namespace xtgui

#endif /* XT_GUI_FORM_INTERNAL_H */
