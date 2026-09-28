/*
 * xbintsc GUI engine — process-level state and the main loop.
 *
 * One SDL_GPU device is shared by every window. `xt_gui_run` owns the UI
 * thread: each frame it pumps SDL events, renders every open window, then
 * services the xbintsc reactor (`xt_loop_poll(0)`) and promise microtasks so
 * sockets, timers and `await` keep working inside a GUI program.
 *
 * The engine is deliberately free of any compiler knowledge: it only uses the
 * public runtime ABI (`rt.h`).
 *
 * Input dispatch and per-frame rendering live in `gui_events.cpp`; the shared
 * process state is declared in `gui_internal.h`.
 */

#include "gui_internal.h"

#include "paint.h"

#include <stdio.h>
#include <string.h>

namespace guidev {

SDL_GPUDevice *g_device = NULL;
int g_sdl_initialized = 0;
int g_quit = 0;
XtGuiWindow g_windows[XT_GUI_MAX_WINDOWS];
int g_window_count = 0;

/* -- initialisation ------------------------------------------------------- */

static int ensure_init(void) {
  if (g_sdl_initialized) return 1;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "xt_gui: SDL_Init failed: %s\n", SDL_GetError());
    return 0;
  }
  /* Accept every shader format so the same code runs on Metal, Vulkan and
   * D3D12; the backend only needs the one it supports. */
  SDL_GPUShaderFormat formats =
      SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
  g_device = SDL_CreateGPUDevice(formats, false, NULL);
  if (g_device == NULL) {
    fprintf(stderr, "xt_gui: SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 0;
  }
  g_sdl_initialized = 1;
  return 1;
}

/* -- window lookup -------------------------------------------------------- */

static XtGuiWindow *window_by_object(xt_value object) {
  xt_value index_value = xt_object_get_cstr(object, "__xt_gui_index");
  if (!XT_IS_NUMBER(index_value)) return NULL;
  int index = (int)xt_to_number(index_value);
  if (index < 0 || index >= g_window_count) return NULL;
  return &g_windows[index];
}

}  // namespace guidev

using namespace guidev;

XtGuiWindow *xt_gui_window_from_this(xt_value thisValue) {
  return window_by_object(thisValue);
}

void xt_gui_quit_window(XtGuiWindow *win) {
  if (win == NULL || !win->open) return;
  win->open = 0;
  xt_gui_emit(win, "close");
  win->animation_frames.clear();
  xt_gui_handles_reset(win);
  win->document_object = XT_UNDEFINED;
  win->document.reset();
  win->html.clear();
  if (g_device != NULL) {
    xt_gui_geometry_destroy(g_device, &win->geometry);
    xt_gui_geometry_destroy(g_device, &win->text_geometry);
    xt_gui_geometry_destroy(g_device, &win->image_geometry);
  }
  if (win->window != NULL && g_device != NULL) {
    SDL_ReleaseWindowFromGPUDevice(g_device, win->window);
  }
  if (win->window != NULL) SDL_DestroyWindow(win->window);
  win->window = NULL;
}

void xt_gui_window_viewport(XtGuiWindow *win, float *width, float *height) {
  int w = win->width;
  int h = win->height;
  if (win->window != NULL) SDL_GetWindowSize(win->window, &w, &h);
  if (width != NULL) *width = (float)w;
  if (height != NULL) *height = (float)h;
}

/* -- createWindow(options) ------------------------------------------------ */

static const char *xt_gui_string_or(xt_value value, const char *fallback) {
  if (!XT_IS_STRING(value)) return fallback;
  return xt_string_data(value);
}

static int xt_gui_int_or(xt_value value, int fallback) {
  if (!XT_IS_NUMBER(value)) return fallback;
  return (int)xt_to_number(value);
}

extern "C" xt_value xt_gui_create_window(int32_t argc, xt_value *argv) {
  if (!ensure_init()) return XT_UNDEFINED;
  if (g_window_count >= XT_GUI_MAX_WINDOWS) {
    fprintf(stderr, "xt_gui: too many windows (max %d)\n", XT_GUI_MAX_WINDOWS);
    return XT_UNDEFINED;
  }

  const char *title = "xbintsc gui";
  int width = 800;
  int height = 600;
  xt_value options = xt_arg(argc, argv, 0);
  if (XT_IS_OBJECT(options)) {
    title = xt_gui_string_or(xt_object_get_cstr(options, "title"), title);
    width = xt_gui_int_or(xt_object_get_cstr(options, "width"), width);
    height = xt_gui_int_or(xt_object_get_cstr(options, "height"), height);
  } else if (XT_IS_STRING(options)) {
    title = xt_string_data(options);
    width = xt_gui_int_or(xt_arg(argc, argv, 1), width);
    height = xt_gui_int_or(xt_arg(argc, argv, 2), height);
  }

  SDL_Window *window = SDL_CreateWindow(
      title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == NULL) {
    fprintf(stderr, "xt_gui: SDL_CreateWindow failed: %s\n", SDL_GetError());
    return XT_UNDEFINED;
  }
  if (!SDL_ClaimWindowForGPUDevice(g_device, window)) {
    fprintf(stderr, "xt_gui: SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    return XT_UNDEFINED;
  }

  int index = g_window_count++;
  XtGuiWindow *record = &g_windows[index];
  record->window = window;
  record->open = 1;
  record->ready = 0;
  record->width = width;
  record->height = height;
  record->html.clear();
  record->document.reset();
  record->geometry = XtGuiGeometry();
  record->text_geometry = XtGuiGeometry();
  record->image_geometry = XtGuiGeometry();
  record->last_frame_ms = 0.0;
  record->animation_frames.clear();
  record->next_animation_frame_id = 1;
  record->background[0] = 0.08f;
  record->background[1] = 0.09f;
  record->background[2] = 0.11f;
  record->background[3] = 1.0f;

  xt_value object = xt_object_new_with_proto(xt_gui_window_proto());
  record->object = object;
  xt_object_set(object, xt_string_from_cstr("__xt_gui_index"), xt_number((double)index));
  xt_object_set(object, xt_string_from_cstr("__xt_gui_html"), xt_string_from_cstr(""));
  xt_object_set(object, xt_string_from_cstr("__xt_gui_events"), xt_object_new());

  return object;
}

/* -- run() / quit() ------------------------------------------------------- */

extern "C" xt_value xt_gui_run(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  /* Test/CI hook: exit after N ms even if windows are open. */
  Uint64 deadline = 0;
  const char *autoclose = SDL_getenv("XT_GUI_AUTOCLOSE_MS");
  if (autoclose != NULL && autoclose[0] != '\0') {
    deadline = SDL_GetTicks() + (Uint64)SDL_atoi(autoclose);
  }
  while (!g_quit) {
    SDL_Event event;
    int had_event = 0;
    while (SDL_PollEvent(&event)) {
      had_event = 1;
      handle_event(&event);
    }

    int open = 0;
    for (int i = 0; i < g_window_count; i++) {
      if (!g_windows[i].open) continue;
      open++;
      xt_gui_render_window(&g_windows[i]);
    }
    if (open == 0) break;

    /* Let sockets, timers and promise continuations make progress between
     * frames. `xt_loop_poll(0)` is non-blocking. */
    xt_loop_poll(0);
    xt_drain_microtasks();

    if (deadline != 0 && SDL_GetTicks() >= deadline) break;
    if (!had_event) SDL_Delay(1);
  }

  SDL_GPUDevice *device = g_device;
  if (device != NULL) {
    SDL_WaitForGPUIdle(device);
    for (int i = 0; i < g_window_count; i++) {
      if (g_windows[i].open) xt_gui_quit_window(&g_windows[i]);
    }
    xt_gui_renderer_destroy(device);
    SDL_DestroyGPUDevice(device);
    g_device = NULL;
  }
  if (g_sdl_initialized) {
    SDL_Quit();
    g_sdl_initialized = 0;
  }
  g_window_count = 0;
  return XT_UNDEFINED;
}

extern "C" xt_value xt_gui_quit(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  g_quit = 1;
  for (int i = 0; i < g_window_count; i++) {
    if (g_windows[i].open) xt_gui_quit_window(&g_windows[i]);
  }
  return XT_UNDEFINED;
}
