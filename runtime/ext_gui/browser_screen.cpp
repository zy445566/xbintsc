/*
 * xbintsc GUI engine — the `screen` global.
 *
 * A browser page reads `screen.width` / `screen.height` to size a layout or pick
 * a quality tier, so the object describes the *display* (desktop mode and usable
 * bounds) rather than the window, which is what `innerWidth` is for. It is built
 * once and rooted, because `screen` is a stable singletons in a browser too.
 *
 * The window prototype getter is installed by `browser.cpp`; the member also
 * shows up as a bare `screen` global through the extension's window-member table
 * (`src/extensions/gui/index.ts`).
 */

#include "gui_engine.h"

namespace {

xt_value g_screen = XT_UNDEFINED;

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_number(xt_value target, const char *name, double value) {
  set_prop(target, name, xt_number(value));
}

}  // namespace

/** `screen`: the desktop display the window is on.
 *
 * The engine falls back to the window size when SDL cannot name a display (a
 * headless host), so a page never sees a zero-sized screen. */
xt_value xt_gui_screen_object(xt_value self) {
  if (XT_IS_OBJECT(g_screen)) return g_screen;
  SDL_DisplayID display = SDL_GetPrimaryDisplay();
  const SDL_DisplayMode *mode = display != 0 ? SDL_GetDesktopDisplayMode(display) : NULL;
  int width = mode != NULL ? mode->w : 0;
  int height = mode != NULL ? mode->h : 0;
  if (width <= 0 || height <= 0) {
    XtGuiWindow *win = xt_gui_window_from_this(self);
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    if (win != NULL) xt_gui_window_viewport(win, &viewportWidth, &viewportHeight);
    width = viewportWidth > 0.0f ? (int)viewportWidth : 1920;
    height = viewportHeight > 0.0f ? (int)viewportHeight : 1080;
  }
  int availWidth = width;
  int availHeight = height;
  SDL_Rect usable;
  if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable)) {
    if (usable.w > 0 && usable.h > 0) {
      availWidth = usable.w;
      availHeight = usable.h;
    }
  }
  xt_value screen = xt_object_new();
  set_number(screen, "width", (double)width);
  set_number(screen, "height", (double)height);
  set_number(screen, "availWidth", (double)availWidth);
  set_number(screen, "availHeight", (double)availHeight);
  set_number(screen, "colorDepth", 24);
  set_number(screen, "pixelDepth", 24);
  /* `screen.orientation` is part of the Screen Orientation API; a desktop
   * display is always landscape-primary. */
  xt_value orientation = xt_object_new();
  set_prop(orientation, "type", xt_string_from_cstr(width >= height ? "landscape-primary" : "portrait-primary"));
  set_number(orientation, "angle", 0);
  set_prop(screen, "orientation", orientation);
  g_screen = screen;
  xt_gc_add_root(&g_screen);
  return screen;
}
