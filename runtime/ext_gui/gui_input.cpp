/*
 * xbintsc GUI engine — SDL event translation.
 *
 * The frame loop polls SDL and hands every event to `handle_event`, which finds
 * the owning window and turns the event into the engine's DOM-shaped callback.
 * Hit testing, pointer payloads and rendering stay in `gui_events.cpp`; keeping
 * the SDL switch here leaves that unit under the repository's size budget and
 * focused on delivery.
 */

#include "gui_internal.h"

#include <string.h>

namespace guidev {

namespace {

/** The live window record that owns an SDL window id, or null. */
XtGuiWindow *find_by_window_id(SDL_WindowID id) {
  SDL_Window *window = SDL_GetWindowFromID(id);
  if (window == NULL) return NULL;
  for (int i = 0; i < g_window_count; i++) {
    if (g_windows[i].window == window) return &g_windows[i];
  }
  return NULL;
}

}  // namespace

void handle_event(const SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    g_quit = 1;
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    XtGuiWindow *win = find_by_window_id(event->window.windowID);
    if (win != NULL && win->open) xt_gui_quit_window(win);
    return;
  }
  /* Losing focus releases the pointer lock, as the browser does when the user
   * switches away: leaving the cursor hidden in another window would trap the
   * pointer with no way back except Escape. */
  if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    XtGuiWindow *win = find_by_window_id(event->window.windowID);
    if (win != NULL && win->pointer_lock_node != nullptr) {
      xt_gui_set_pointer_lock(win, nullptr);
    }
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_RESIZED) {
    XtGuiWindow *win = find_by_window_id(event->window.windowID);
    if (win == NULL || !win->open) return;
    /* `setSize` already applied and emitted the change synchronously; the
     * queued backend event must not deliver the same resize twice. */
    if (win->width == event->window.data1 && win->height == event->window.data2) return;
    win->width = event->window.data1;
    win->height = event->window.data2;
    if (win->document != NULL) win->document->restyle((float)win->width, (float)win->height);
    win->geometry.dirty = 1;
    win->present_dirty = 1;
    /* Web 'resize', so a document can re-measure on a window change in every
     * host (a browser already fires one). */
    xt_gui_emit(win, "resize");
    xt_gui_media_lists_notify(win);
    return;
  }
  /* A window the compositor revealed (uncovered, restored, shown) must be
   * redrawn even though nothing in the document changed: the frame loop only
   * presents on demand. The request is charged to `exposes` (below) so
   * `stats()` can separate it from a present the page caused. */
  if (event->type == SDL_EVENT_WINDOW_EXPOSED || event->type == SDL_EVENT_WINDOW_SHOWN ||
      event->type == SDL_EVENT_WINDOW_RESTORED) {
    XtGuiWindow *win = find_by_window_id(event->window.windowID);
    if (win != NULL) {
      win->present_dirty = 1;
      win->expose_pending = 1;
    }
    return;
  }

  if (event->type == SDL_EVENT_TEXT_INPUT) {
    XtGuiWindow *win = find_by_window_id(event->text.windowID);
    if (win == NULL || !win->open) return;
    xt_gui_text_input(win, event->text.text);
    return;
  }
  if (event->type == SDL_EVENT_TEXT_EDITING) {
    XtGuiWindow *win = find_by_window_id(event->edit.windowID);
    if (win == NULL || !win->open) return;
    xt_gui_text_editing(win, event->edit.text, event->edit.start, event->edit.length);
    return;
  }

  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    XtGuiWindow *win = find_by_window_id(event->motion.windowID);
    /* SDL's `xrel`/`yrel` are the raw delta and stay meaningful in relative
     * (pointer-lock) mode, where `x`/`y` stop tracking a visible cursor. */
    xt_gui_dispatch_pointer(win, "mousemove", event->motion.x, event->motion.y, -1, 0,
                            event->motion.xrel, event->motion.yrel);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    XtGuiWindow *win = find_by_window_id(event->button.windowID);
    /* SDL numbers buttons 1/2/3 (left/middle/right); DOM uses 0/1/2. */
    xt_gui_dispatch_pointer(win, "mousedown", event->button.x, event->button.y,
                            (int)event->button.button - 1, event->button.clicks);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    XtGuiWindow *win = find_by_window_id(event->button.windowID);
    if (win == NULL || !win->open) return;
    int button = (int)event->button.button - 1;
    xt_gui_dispatch_pointer(win, "mouseup", event->button.x, event->button.y, button,
                            event->button.clicks);
    if (button == 0) {
      xt_gui_dispatch_pointer(win, "click", event->button.x, event->button.y, button,
                              event->button.clicks);
    }
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    XtGuiWindow *win = find_by_window_id(event->wheel.windowID);
    /* DOM `deltaY` grows downwards; SDL's grows away from the user. */
    xt_gui_dispatch_wheel(win, event->wheel.mouse_x, event->wheel.mouse_y, event->wheel.x,
                          -event->wheel.y);
    return;
  }
  if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
    XtGuiWindow *win = find_by_window_id(event->key.windowID);
    if (win == NULL || !win->open) return;
    /* Escape is the browser's universal exit from pointer lock; release before
     * the page sees the key, so its handler observes the unlocked state. */
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE &&
        win->pointer_lock_node != nullptr) {
      xt_gui_set_pointer_lock(win, nullptr);
    }
    const char *type = event->type == SDL_EVENT_KEY_DOWN ? "keydown" : "keyup";
    xt_gui_dispatch_physical_key(
        win, type, static_cast<int>(event->key.scancode), static_cast<int>(event->key.key),
        event->key.repeat, (event->key.mod & SDL_KMOD_CTRL) != 0,
        (event->key.mod & SDL_KMOD_SHIFT) != 0, (event->key.mod & SDL_KMOD_ALT) != 0,
        (event->key.mod & SDL_KMOD_GUI) != 0, (event->key.mod & SDL_KMOD_CAPS) != 0);
    return;
  }
}

}  // namespace guidev
