/*
 * xbintsc GUI engine — CSS `cursor` → SDL system cursor.
 *
 * Kept apart from `gui_events.cpp` (which owns the input dispatch) so both
 * translation units stay under the repository's per-file line budget.
 */

#include "gui_internal.h"

namespace guidev {

namespace {

/** Lazily created SDL system cursors for the CSS cursor values we map. */
SDL_Cursor *g_system_cursors[9] = {};

SDL_SystemCursor sdlCursorFor(xtgui::CursorShape shape) {
  switch (shape) {
    case xtgui::CursorShape::Pointer: return SDL_SYSTEM_CURSOR_POINTER;
    case xtgui::CursorShape::Text: return SDL_SYSTEM_CURSOR_TEXT;
    case xtgui::CursorShape::Move: return SDL_SYSTEM_CURSOR_MOVE;
    case xtgui::CursorShape::Wait: return SDL_SYSTEM_CURSOR_WAIT;
    case xtgui::CursorShape::Help: return SDL_SYSTEM_CURSOR_DEFAULT;
    case xtgui::CursorShape::NotAllowed: return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case xtgui::CursorShape::Crosshair: return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case xtgui::CursorShape::Auto:
    case xtgui::CursorShape::Default: return SDL_SYSTEM_CURSOR_DEFAULT;
  }
  return SDL_SYSTEM_CURSOR_DEFAULT;
}

}  // namespace

/** Apply the hovered element's computed `cursor` to the window. */
void applyCursor(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || win->window == nullptr) return;
  xtgui::CursorShape shape = xtgui::CursorShape::Default;
  if (node != nullptr && win->document != nullptr) {
    const xtgui::XtStyle *style = win->document->styleOf(node);
    if (style != nullptr) shape = style->cursor;
  }
  int index = (int)shape;
  if (index < 0 || index >= 9) index = 0;
  if (g_system_cursors[index] == nullptr) {
    g_system_cursors[index] = SDL_CreateSystemCursor(sdlCursorFor(shape));
  }
  if (g_system_cursors[index] != nullptr) SDL_SetCursor(g_system_cursors[index]);
}

}  // namespace guidev
