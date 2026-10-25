/*
 * xbintsc GUI engine — internal header.
 *
 * The engine owns one SDL3 window + SDL_GPU device per `createWindow(...)`
 * call. Windows are exposed to TypeScript as runtime objects whose methods live
 * on a shared prototype (see `window.cpp`). The three `xt_gui_*` entry points
 * below use the uniform `(argc, argv)` ABI the extension bindings expect.
 *
 * SDL3 and its dependencies are linked into `gui.a`; only the SDL3 headers
 * (`SDL3/SDL.h`) are needed to build these translation units.
 */
#ifndef XT_GUI_ENGINE_H
#define XT_GUI_ENGINE_H

#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Uniform `(argc, argv)` entry points bound by `src/extensions/gui`. */
xt_value xt_gui_create_window(int32_t argc, xt_value *argv);
xt_value xt_gui_run(int32_t argc, xt_value *argv);
xt_value xt_gui_quit(int32_t argc, xt_value *argv);
/** `__registerScript(id, fn)`: record an AOT-compiled `<script>` body. */
xt_value xt_register_script(int32_t argc, xt_value *argv);
/** Which SDL_GPU backend the device is using (`"vulkan"`, `"direct3d12"`,
 * `"metal"`, …), or `""` before the device exists. */
const char *xt_gui_driver(void);

/** The window handle the browser globals (`window`, `document`) refer to: the
 * most recent live one, opened on demand. `createWindow` adopts that implicit
 * window instead of opening a second one. */
xt_value xt_gui_ensure_window(void);

/* The browser globals a page reaches by bare name. Code generation lowers each
 * identifier (or bare call) to one of these; nothing is declared in the source.
 * See `src/extensions/gui/index.ts` (`globalValues` / `builtins`) and
 * `runtime/ext_gui/browser.cpp`. */
xt_value xt_browser_window(int32_t argc, xt_value *argv);
xt_value xt_browser_document(int32_t argc, xt_value *argv);
xt_value xt_browser_navigator(int32_t argc, xt_value *argv);
xt_value xt_browser_local_storage(int32_t argc, xt_value *argv);
/** A global the host deliberately does not provide: `undefined`. */
xt_value xt_browser_missing(int32_t argc, xt_value *argv);
/** A DOM type name (`Image`, `HTMLCanvasElement`, …) as a constructor value. */
xt_value xt_browser_type(int32_t argc, xt_value *argv);
xt_value xt_browser_request_animation_frame(int32_t argc, xt_value *argv);
xt_value xt_browser_cancel_animation_frame(int32_t argc, xt_value *argv);
xt_value xt_browser_match_media(int32_t argc, xt_value *argv);
xt_value xt_browser_computed_style(int32_t argc, xt_value *argv);
xt_value xt_browser_escape(int32_t argc, xt_value *argv);
xt_value xt_browser_alert(int32_t argc, xt_value *argv);
xt_value xt_browser_prompt(int32_t argc, xt_value *argv);
/** Install `innerWidth`, `setTimeout`, `navigator`, … on the window prototype. */
void xt_browser_install_window(xt_value proto);
/** Poll and dispatch pending SDL input events once (the browser frame pump in
 * `browser.cpp` calls it; `run()` has its own loop). */
void xt_gui_pump_events(void);
/** Arm the frame pump for a page that drives itself from
 * `requestAnimationFrame` instead of calling `run()` (see `browser_loop.cpp`). */
void xt_browser_schedule_frame(void);
/** Whether `run()` currently owns the frame loop (the pump then stays idle). */
int xt_gui_run_active(void);

/* Web Audio objects (`audio.cpp`). */
/** `AudioContext.prototype` — also the `instanceof AudioContext` target. */
xt_value xt_gui_audio_proto(void);
/** `new AudioContext(...)`: it has no mixer backend yet, so the graph records
 * its shape and parameters but nothing is rendered to the sound device. */
xt_value xt_gui_audio_create(int32_t argc, xt_value *argv);

#ifdef __cplusplus
} /* extern "C" */

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "canvas.h"
#include "document.h"
#include "media.h"
#include "renderer.h"

/** One element-level event listener registered through `addEventListener`. */
struct XtGuiNodeListener {
  std::string type;
  xt_value fn;
  bool once;
  bool capture;
};

/** One pending `requestAnimationFrame` callback. */
struct XtGuiAnimationFrame {
  int id;
  xt_value fn;
};

/** One row of the window's node table: the node a handle addresses, plus the
 * generation that row was issued for. A slot is recycled after its node is
 * forgotten, and the generation is what keeps an old handle from resolving
 * through the recycled slot to an unrelated node. */
struct XtGuiNodeSlot {
  const xtgui::Node *node = nullptr;
  int generation = 0;
};

/** One media element's decoder slot. A failed open (bad `src`, or a build
 * without a decoder) is remembered so the engine does not retry it every
 * frame and fire `error` repeatedly. */
struct XtGuiMediaEntry {
  std::unique_ptr<xtgui::MediaPlayer> player;
  std::string src;
  bool failed = false;
  /** Decoder generation already handed to the image upload, so a new video
   * frame can mark the geometry dirty exactly once. */
  uint64_t rendered_generation = 0;
};

/* One live window. `object` is the JavaScript-visible handle; the record is
 * addressed from it through the hidden `__xt_gui_index` property. */
struct XtGuiWindow {
  SDL_Window *window;
  xt_value object;
  int open;
  double last_frame_ms;
  int ready;
  std::string html;
  std::unique_ptr<xtgui::XtDocument> document;
  XtGuiGeometry geometry;
  XtGuiGeometry text_geometry;
  XtGuiGeometry image_geometry;
  float background[4];
  int width;
  int height;

  /* -- DOM object model (element handles + events) ----------------------- */
  /** `document` handle for this window (lazily created). */
  xt_value document_object = XT_UNDEFINED;
  /** Bumped on every `loadHTML`; stale handles no-op. */
  double doc_generation = 1.0;
  /** Node -> table index / cached handle, so identity is stable. */
  std::vector<XtGuiNodeSlot> node_order;
  /** Slots whose node was forgotten and which a later handle may reuse. */
  std::vector<int> free_node_slots;
  std::unordered_map<const xtgui::Node *, int> node_index;
  std::unordered_map<const xtgui::Node *, xt_value> node_handles;
  /** Element-level listeners, keyed by node. */
  std::unordered_map<const xtgui::Node *, std::vector<XtGuiNodeListener>> node_listeners;
  /** Set by a DOM mutation; consumed by the frame loop before repainting. */
  int struct_dirty = 0;
  /** Callbacks queued for the next frame, plus the id counter. */
  std::vector<XtGuiAnimationFrame> animation_frames;
  /** One decoder per `<video>`/`<audio>` element, keyed by the element node. The
   * map doubles as the set of elements whose media is loaded; `gui_media.cpp`
   * reconciles it with the live DOM every frame. */
  std::unordered_map<const xtgui::Node *, XtGuiMediaEntry> media_players;
  /** One `Canvas2D` per `<canvas>` element, keyed by the element node. Created
   * lazily by `getContext("2d")` (or by `xt_gui_canvas_sync` for a live canvas),
   * released when the node is forgotten or the document reloads. */
  std::unordered_map<const xtgui::Node *, std::unique_ptr<xtgui::Canvas2D>> canvases;
  /** The batch currently running: its callbacks have been swapped out of
   * `animation_frames`, and a collection triggered by one of them must still
   * see the rest. Kept on the record (not a local) so the GC root provider can
   * mark it. Empty except while `xt_gui_run_animation_frames` runs. */
  std::vector<XtGuiAnimationFrame> running_frames;
  int next_animation_frame_id = 1;
};

/** Resolve a window handle (`this`) to its record, or NULL for a foreign value. */
XtGuiWindow *xt_gui_window_from_this(xt_value thisValue);


/** The `document` handle for `win` (created on first use). */
xt_value xt_gui_document_handle(XtGuiWindow *win);
/** Node handle for `node` (created and cached on first use). */
xt_value xt_gui_node_handle(XtGuiWindow *win, const xtgui::Node *node);
/** Forget a node's slot and handle because it is about to be destroyed (a
 * removal that frees it, or `innerHTML = …` dropping the old children). Without
 * this the node table would keep a dangling pointer. */
void xt_gui_node_handle_forget(XtGuiWindow *win, const xtgui::Node *node);
/** Drop every cached handle/listener and bump the document generation. */
void xt_gui_handles_reset(XtGuiWindow *win);
/** Deliver `type` to element listeners along the propagation path. */
void xt_gui_emit_dom_event(XtGuiWindow *win, const char *type, const xtgui::Node *target,
                           xt_value legacy_payload);
/** Synthesise a DOM event whose target is `target` (used by element.click()). */
void xt_gui_dispatch_to_node(XtGuiWindow *win, const xtgui::Node *target, const char *type);
/** Apply any pending DOM mutation (restyle/relayout) before a synchronous read. */
void xt_gui_flush_dom(XtGuiWindow *win);
/** Run every registered `<script data-xt-id>` body found in the document, in
 * document order, passing `(windowHandle, documentHandle)`. */
void xt_gui_run_scripts(XtGuiWindow *win);
/** GC root provider: mark every AOT-compiled `<script>` function in the
 * process-wide registry. Registered with the collector by `xt_register_script`. */
void xt_gui_script_gc_scan(void);
/** Shared prototype carrying the window methods. */
xt_value xt_gui_window_proto(void);
/** Release GPU claim + destroy the window and mark the record closed. */
void xt_gui_quit_window(XtGuiWindow *win);
/** Deliver `event` (no arguments) to handlers registered with `on`. */
void xt_gui_emit(XtGuiWindow *win, const char *event);
/** Deliver `event` with a single `payload` argument (input events). */
void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload);
/** Acquire a swapchain frame for `win` and clear it (placeholder paint). */
void xt_gui_render_window(XtGuiWindow *win);
/** Run every callback queued with `requestAnimationFrame` for the frame at
 * `timestamp_ms`, then clear the queue. */
void xt_gui_run_animation_frames(XtGuiWindow *win, double timestamp_ms);
/** Current logical window size (CSS viewport), in pixels. */
void xt_gui_window_viewport(XtGuiWindow *win, float *width, float *height);
/** CSS-like descriptor of the deepest element at (x, y), or `""` (test/debug). */
std::string xt_gui_hit_test(XtGuiWindow *win, float x, float y);
/** Synthesise a pointer event (`mousemove`/`mousedown`/`mouseup`/`click`). */
void xt_gui_dispatch_pointer(XtGuiWindow *win, const char *type, float x, float y, int button,
                             int clicks);
/** Synthesise a wheel event. */
void xt_gui_dispatch_wheel(XtGuiWindow *win, float x, float y, float delta_x, float delta_y);
/** Synthesise a key event (`keydown`/`keyup`). */
void xt_gui_dispatch_key(XtGuiWindow *win, const char *type, const char *key, const char *code);

/** The decoder for one media element, opening it (and its `src`) on first use.
 * Returns `nullptr` for a non-media element or an unreadable source. */
xtgui::MediaPlayer *xt_gui_media_player(XtGuiWindow *win, const xtgui::Node *node);
/** Close and drop the decoder for one element (e.g. its `src` changed). */
void xt_gui_media_forget(XtGuiWindow *win, const xtgui::Node *node);
/** Reconcile the player table with the live DOM: open new `<video>`/`<audio>`
 * sources, drop players whose element is gone, and record decoded sizes so the
 * next layout gives a `<video>` its intrinsic box. */
void xt_gui_media_sync(XtGuiWindow *win);
/** Advance every decoder and dispatch the resulting DOM events. */
void xt_gui_media_tick(XtGuiWindow *win, double now_ms);
/** Release all decoders and their GPU textures (window close / `loadHTML`). */
void xt_gui_media_shutdown(XtGuiWindow *win);

/** The 2D context of a `<canvas>` element, creating it (at the element's
 * `width`/`height` attributes, 300x150 by default) on first use. */
xtgui::Canvas2D *xt_gui_canvas_get(XtGuiWindow *win, const xtgui::Node *node);
/** The existing 2D context of `node`, or null (no side effects). */
xtgui::Canvas2D *xt_gui_canvas_find(XtGuiWindow *win, const xtgui::Node *node);
/** Drop a node's canvas (called when its handle is forgotten). */
void xt_gui_canvas_forget(XtGuiWindow *win, const xtgui::Node *node);
/** Publish live canvases to the document (sizes + render ids) so layout and
 * paint see them; creates a context for a canvas that has none yet. */
void xt_gui_canvas_sync(XtGuiWindow *win);
/** Release every canvas of `win` (window close / `loadHTML`). */
void xt_gui_canvas_shutdown(XtGuiWindow *win);

#endif /* __cplusplus */

#endif /* XT_GUI_ENGINE_H */
