/*
 * xbintsc GUI engine — the frame pump for browser-style pages.
 *
 * A program that calls `run()` hands the frame loop to the engine: `xt_gui_run`
 * polls input, renders and drains microtasks until the last window closes. A
 * *page* — the kind xbintsc compiles straight from browser code — never calls
 * `run()`. It asks for the next frame with `requestAnimationFrame` and expects
 * the host to keep serving frames; without a pump the window would open and
 * then never paint.
 *
 * This unit closes that gap with one repeating runtime timer. Each tick
 * dispatches input, renders (which also drains the queued animation-frame
 * callbacks before layout, so a mutation a callback makes lands in the same
 * frame) and re-arms itself only while callbacks are still queued. A page that
 * stops animating therefore lets the event loop drain, and the program exits on
 * its own — the same lifecycle a browser tab has when it goes idle.
 *
 * `XT_GUI_AUTOCLOSE_MS` is honoured here too, so a headless run of such a page
 * cannot hang.
 */

#include "gui_engine.h"

namespace {

/** The tick callback, built once and rooted for the life of the program. */
xt_value g_tick = XT_UNDEFINED;
int g_tick_pending = 0;
double g_tick_started_ms = 0;
double g_tick_deadline_ms = 0;

xt_value browser_tick(xt_value self, xt_value env, int32_t argc, xt_value *argv);

void arm_tick(void) {
  /* `run()` renders its own frames; a second pump would paint each window
   * twice per tick. */
  if (g_tick_pending || xt_gui_run_active()) return;
  if (!XT_IS_FUNCTION(g_tick)) {
    g_tick = xt_closure_new((void *)browser_tick, 0, NULL);
    xt_gc_add_root(&g_tick);
  }
  xt_value args[2] = {g_tick, xt_number(16)};
  g_tick_pending = 1;
  xt_set_timeout(2, args);
}

xt_value browser_tick(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  g_tick_pending = 0;
  if (xt_gui_run_active()) return XT_UNDEFINED;
  if (g_tick_deadline_ms > 0 && (double)SDL_GetTicks() >= g_tick_deadline_ms) {
    /* Shared with `run()`: stop animating and let the program exit instead of
     * hanging a headless run. */
    xt_gui_quit(0, NULL);
    return XT_UNDEFINED;
  }
  xt_value win = xt_browser_window(0, NULL);
  if (XT_IS_UNDEFINED(win)) return XT_UNDEFINED;
  XtGuiWindow *record = xt_gui_window_from_this(win);
  if (record == NULL || !record->open) return XT_UNDEFINED;
  xt_gui_pump_events();
  xt_gui_render_window(record);
  if (record->open && !record->animation_frames.empty()) arm_tick();
  return XT_UNDEFINED;
}

}  // namespace

/** Make sure a frame will be served: start the clock on the first request and
 * arm the repeating tick unless one is already queued. */
extern "C" void xt_browser_schedule_frame(void) {
  if (g_tick_started_ms == 0.0) {
    g_tick_started_ms = (double)SDL_GetTicks();
    const char *autoclose = SDL_getenv("XT_GUI_AUTOCLOSE_MS");
    if (autoclose != NULL && autoclose[0] != '\0') {
      g_tick_deadline_ms = g_tick_started_ms + (double)SDL_atoi(autoclose);
    }
  }
  arm_tick();
}
