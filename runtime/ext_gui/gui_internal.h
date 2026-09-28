/*
 * xbintsc GUI engine — process-level state shared by `gui.cpp` (lifecycle and
 * the main loop) and `gui_events.cpp` (input dispatch and rendering).
 *
 * One SDL_GPU device is shared by every window. The state used to be a set of
 * file-statics in `gui.cpp`; it now has external linkage inside `guidev` so the
 * event/render unit can reach it.
 */
#ifndef XT_GUI_INTERNAL_H
#define XT_GUI_INTERNAL_H

#include "gui_engine.h"

#define XT_GUI_MAX_WINDOWS 64

namespace guidev {

extern SDL_GPUDevice *g_device;
extern int g_sdl_initialized;
extern int g_quit;
extern XtGuiWindow g_windows[XT_GUI_MAX_WINDOWS];
extern int g_window_count;

/** Translate one SDL event into engine callbacks (defined in gui_events.cpp). */
void handle_event(const SDL_Event *event);

}  // namespace guidev

#endif /* XT_GUI_INTERNAL_H */
