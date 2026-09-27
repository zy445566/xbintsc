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
 */

#include "gui_engine.h"

#include <stdio.h>
#include <string.h>

#define XT_GUI_MAX_WINDOWS 64

static SDL_GPUDevice *g_device = NULL;
static int g_sdl_initialized = 0;
static int g_quit = 0;
static XtGuiWindow g_windows[XT_GUI_MAX_WINDOWS];
static int g_window_count = 0;

/* -- initialisation ------------------------------------------------------- */

static int xt_gui_ensure_init(void) {
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

static XtGuiWindow *xt_gui_window_by_object(xt_value object) {
  xt_value index_value = xt_object_get_cstr(object, "__xt_gui_index");
  if (!XT_IS_NUMBER(index_value)) return NULL;
  int index = (int)xt_to_number(index_value);
  if (index < 0 || index >= g_window_count) return NULL;
  return &g_windows[index];
}

XtGuiWindow *xt_gui_window_from_this(xt_value thisValue) {
  return xt_gui_window_by_object(thisValue);
}

/* -- events --------------------------------------------------------------- */

void xt_gui_emit(XtGuiWindow *win, const char *event) {
  if (win == NULL || !XT_IS_OBJECT(win->object)) return;
  xt_value listeners = xt_object_get_cstr(win->object, "__xt_gui_events");
  if (!XT_IS_OBJECT(listeners)) return;
  xt_value array = xt_object_get(listeners, xt_string_from_cstr(event));
  if (!XT_IS_ARRAY(array)) return;
  int32_t count = (int32_t)xt_to_number(xt_array_length(array));
  for (int32_t i = 0; i < count; i++) {
    xt_value fn = xt_array_get(array, xt_number((double)i));
    if (XT_IS_FUNCTION(fn)) xt_call_with_this(fn, win->object, 0, NULL);
  }
}

void xt_gui_quit_window(XtGuiWindow *win) {
  if (win == NULL || !win->open) return;
  win->open = 0;
  xt_gui_emit(win, "close");
  if (win->window != NULL && g_device != NULL) {
    SDL_ReleaseWindowFromGPUDevice(g_device, win->window);
  }
  if (win->window != NULL) SDL_DestroyWindow(win->window);
  win->window = NULL;
}

static void xt_gui_handle_event(const SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    g_quit = 1;
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    SDL_Window *window = SDL_GetWindowFromID(event->window.windowID);
    if (window == NULL) return;
    for (int i = 0; i < g_window_count; i++) {
      if (g_windows[i].open && g_windows[i].window == window) {
        xt_gui_quit_window(&g_windows[i]);
        return;
      }
    }
  }
}

/* -- rendering ------------------------------------------------------------ */

void xt_gui_render_window(XtGuiWindow *win) {
  if (win == NULL || !win->open || win->window == NULL || g_device == NULL) return;
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
  if (cmd == NULL) return;
  SDL_GPUTexture *swapchain = NULL;
  SDL_AcquireGPUSwapchainTexture(cmd, win->window, &swapchain, NULL, NULL);
  if (swapchain != NULL) {
    SDL_GPUColorTargetInfo target;
    memset(&target, 0, sizeof(target));
    target.texture = swapchain;
    target.clear_color.r = 0.08f;
    target.clear_color.g = 0.09f;
    target.clear_color.b = 0.11f;
    target.clear_color.a = 1.0f;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    SDL_EndGPURenderPass(pass);
    /* The first presented frame marks the window ready; handlers registered
     * after `createWindow` are guaranteed to see this event. */
    if (!win->ready) {
      win->ready = 1;
      xt_gui_emit(win, "ready");
    }
  }
  SDL_SubmitGPUCommandBuffer(cmd);
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
  if (!xt_gui_ensure_init()) return XT_UNDEFINED;
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
      xt_gui_handle_event(&event);
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
