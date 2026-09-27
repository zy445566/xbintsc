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
#include "paint.h"

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
  xt_gui_emit_payload(win, event, XT_UNDEFINED);
}

void xt_gui_emit_payload(XtGuiWindow *win, const char *event, xt_value payload) {
  if (win == NULL || !XT_IS_OBJECT(win->object)) return;
  xt_value listeners = xt_object_get_cstr(win->object, "__xt_gui_events");
  if (!XT_IS_OBJECT(listeners)) return;
  xt_value array = xt_object_get(listeners, xt_string_from_cstr(event));
  if (!XT_IS_ARRAY(array)) return;
  xt_value args[1];
  int32_t count = 0;
  if (!XT_IS_UNDEFINED(payload)) {
    args[0] = payload;
    count = 1;
  }
  int32_t listener_count = (int32_t)xt_to_number(xt_array_length(array));
  for (int32_t i = 0; i < listener_count; i++) {
    xt_value fn = xt_array_get(array, xt_number((double)i));
    if (XT_IS_FUNCTION(fn)) xt_call_with_this(fn, win->object, count, args);
  }
}

void xt_gui_quit_window(XtGuiWindow *win) {
  if (win == NULL || !win->open) return;
  win->open = 0;
  xt_gui_emit(win, "close");
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

static XtGuiWindow *xt_gui_find_by_window_id(SDL_WindowID id) {
  SDL_Window *window = SDL_GetWindowFromID(id);
  if (window == NULL) return NULL;
  for (int i = 0; i < g_window_count; i++) {
    if (g_windows[i].window == window) return &g_windows[i];
  }
  return NULL;
}

/* -- input events --------------------------------------------------------- */

/** Deepest element under (x, y), or NULL. */
static const xtgui::Node *xt_gui_hit_node(XtGuiWindow *win, float x, float y) {
  if (win->document == nullptr) return nullptr;
  const xtgui::LayoutBox *box = win->document->layout().hitTest(x, y);
  const xtgui::Node *node = box != nullptr ? box->node : nullptr;
  while (node != nullptr && !node->isElement()) node = node->parent;
  return node;
}

/** Deepest element under (x, y) as a CSS-like descriptor, or an empty string. */
std::string xt_gui_hit_test(XtGuiWindow *win, float x, float y) {
  const xtgui::Node *node = xt_gui_hit_node(win, x, y);
  return node == nullptr ? std::string() : xtgui::xt_dom_describe(node);
}

/** Update the hovered element from the pointer position; marks paint dirty. */
static void xt_gui_update_hover(XtGuiWindow *win, float x, float y) {
  if (win->document == nullptr) return;
  if (win->document->setHover(xt_gui_hit_node(win, x, y))) win->geometry.dirty = 1;
}

static xt_value xt_gui_pointer_payload(const char *type, XtGuiWindow *win, float x, float y,
                                       int button, int clicks) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(payload, xt_string_from_cstr("x"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("y"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("clientX"), xt_number(x));
  xt_object_set(payload, xt_string_from_cstr("clientY"), xt_number(y));
  xt_object_set(payload, xt_string_from_cstr("button"), xt_number(button));
  xt_object_set(payload, xt_string_from_cstr("clicks"), xt_number(clicks));
  std::string target = xt_gui_hit_test(win, x, y);
  if (!target.empty()) {
    xt_object_set(payload, xt_string_from_cstr("target"), xt_string_from_cstr(target.c_str()));
  }
  return payload;
}

void xt_gui_dispatch_pointer(XtGuiWindow *win, const char *type, float x, float y, int button,
                             int clicks) {
  if (win == NULL || !win->open) return;
  xt_gui_update_hover(win, x, y);
  if (strcmp(type, "mousedown") == 0 && win->document != nullptr) {
    if (win->document->setFocus(xt_gui_hit_node(win, x, y))) win->geometry.dirty = 1;
  }
  xt_gui_emit_payload(win, type, xt_gui_pointer_payload(type, win, x, y, button, clicks));
}

void xt_gui_dispatch_wheel(XtGuiWindow *win, float x, float y, float delta_x, float delta_y) {
  if (win == NULL || !win->open) return;
  xt_value payload = xt_gui_pointer_payload("wheel", win, x, y, -1, 0);
  xt_object_set(payload, xt_string_from_cstr("deltaX"), xt_number(delta_x));
  xt_object_set(payload, xt_string_from_cstr("deltaY"), xt_number(delta_y));
  xt_gui_emit_payload(win, "wheel", payload);
}

static xt_value xt_gui_key_payload(const char *type, const char *key, const char *code, bool repeat,
                                   bool ctrl, bool shift, bool alt, bool meta) {
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  xt_object_set(payload, xt_string_from_cstr("key"), xt_string_from_cstr(key));
  xt_object_set(payload, xt_string_from_cstr("code"), xt_string_from_cstr(code));
  xt_object_set(payload, xt_string_from_cstr("repeat"), repeat ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("ctrl"), ctrl ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("shift"), shift ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("alt"), alt ? XT_TRUE : XT_FALSE);
  xt_object_set(payload, xt_string_from_cstr("meta"), meta ? XT_TRUE : XT_FALSE);
  return payload;
}

void xt_gui_dispatch_key(XtGuiWindow *win, const char *type, const char *key, const char *code) {
  if (win == NULL || !win->open) return;
  xt_gui_emit_payload(win, type, xt_gui_key_payload(type, key, code, false, false, false, false, false));
}

static void xt_gui_handle_event(const SDL_Event *event) {
  if (event->type == SDL_EVENT_QUIT) {
    g_quit = 1;
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->window.windowID);
    if (win != NULL && win->open) xt_gui_quit_window(win);
    return;
  }
  if (event->type == SDL_EVENT_WINDOW_RESIZED) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->window.windowID);
    if (win == NULL || !win->open) return;
    win->width = event->window.data1;
    win->height = event->window.data2;
    if (win->document != NULL) win->document->restyle((float)win->width, (float)win->height);
    win->geometry.dirty = 1;
    return;
  }

  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->motion.windowID);
    xt_gui_dispatch_pointer(win, "mousemove", event->motion.x, event->motion.y, -1, 0);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->button.windowID);
    /* SDL numbers buttons 1/2/3 (left/middle/right); DOM uses 0/1/2. */
    xt_gui_dispatch_pointer(win, "mousedown", event->button.x, event->button.y,
                            (int)event->button.button - 1, event->button.clicks);
    return;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->button.windowID);
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
    XtGuiWindow *win = xt_gui_find_by_window_id(event->wheel.windowID);
    /* DOM `deltaY` grows downwards; SDL's grows away from the user. */
    xt_gui_dispatch_wheel(win, event->wheel.mouse_x, event->wheel.mouse_y, event->wheel.x,
                          -event->wheel.y);
    return;
  }
  if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
    XtGuiWindow *win = xt_gui_find_by_window_id(event->key.windowID);
    if (win == NULL || !win->open) return;
    const char *type = event->type == SDL_EVENT_KEY_DOWN ? "keydown" : "keyup";
    xt_gui_emit_payload(win, type,
                        xt_gui_key_payload(type, SDL_GetKeyName(event->key.key),
                                           SDL_GetScancodeName(event->key.scancode),
                                           event->key.repeat, (event->key.mod & SDL_KMOD_CTRL) != 0,
                                           (event->key.mod & SDL_KMOD_SHIFT) != 0,
                                           (event->key.mod & SDL_KMOD_ALT) != 0,
                                           (event->key.mod & SDL_KMOD_GUI) != 0));
    return;
  }
}

/* -- rendering ------------------------------------------------------------ */

void xt_gui_render_window(XtGuiWindow *win) {
  if (win == NULL || !win->open || win->window == NULL || g_device == NULL) return;
  float viewport_width = 0;
  float viewport_height = 0;
  xt_gui_window_viewport(win, &viewport_width, &viewport_height);

  /* Drive CSS transitions. */
  double now = (double)SDL_GetTicks();
  double delta = win->last_frame_ms > 0.0 ? now - win->last_frame_ms : 0.0;
  win->last_frame_ms = now;
  if (win->document != nullptr && win->document->advance(delta)) win->geometry.dirty = 1;

  /* Rebuild the geometry only when the document or viewport changed. */
  bool can_paint = xt_gui_renderer_ensure(g_device, win->window);
  if (can_paint && win->geometry.dirty && win->document != nullptr) {
    xtgui::DisplayList list;
    xtgui::xt_paint_build(win->document->layout().root(), list);
    xt_gui_geometry_upload(g_device, &win->geometry, list);
    float density = SDL_GetWindowPixelDensity(win->window);
    xt_gui_text_upload(g_device, &win->text_geometry, list, density > 0.0f ? density : 1.0f);
    xt_gui_image_upload(g_device, &win->image_geometry, list);
    win->geometry.dirty = 0;
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
  if (cmd == NULL) return;
  SDL_GPUTexture *swapchain = NULL;
  SDL_AcquireGPUSwapchainTexture(cmd, win->window, &swapchain, NULL, NULL);
  bool firstFrame = false;
  if (swapchain != NULL) {
    SDL_GPUColorTargetInfo target;
    memset(&target, 0, sizeof(target));
    target.texture = swapchain;
    target.clear_color.r = win->background[0];
    target.clear_color.g = win->background[1];
    target.clear_color.b = win->background[2];
    target.clear_color.a = win->background[3];
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
    if (can_paint) {
      xt_gui_renderer_draw(cmd, pass, &win->geometry, viewport_width, viewport_height);
      xt_gui_text_draw(cmd, pass, &win->text_geometry, viewport_width, viewport_height);
      xt_gui_image_draw(cmd, pass, &win->image_geometry, viewport_width, viewport_height);
    }
    SDL_EndGPURenderPass(pass);
    firstFrame = !win->ready;
    win->ready = 1;
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  /* The first presented frame marks the window ready. Emitting after the
   * submit lets a `ready` handler safely close/destroy the window. */
  if (firstFrame) xt_gui_emit(win, "ready");
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
  record->width = width;
  record->height = height;
  record->html.clear();
  record->document.reset();
  record->geometry = XtGuiGeometry();
  record->text_geometry = XtGuiGeometry();
  record->image_geometry = XtGuiGeometry();
  record->last_frame_ms = 0.0;
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
