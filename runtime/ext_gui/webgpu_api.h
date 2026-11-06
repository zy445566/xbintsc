/*
 * xbintsc GUI engine — optional WebGPU canvas/context bridge.
 *
 * The WebGPU DOM surface is large and lives behind the `webgpu` link feature.
 * The engine's canvas and paint code reaches it through this table, exactly the
 * way it reaches WebGL through `webgl_api.h`: without the feature the table is
 * null, `getContext("webgpu")` answers null and a canvas's colour texture is
 * never redirected.
 */
#ifndef XT_GUI_WEBGPU_API_H
#define XT_GUI_WEBGPU_API_H

#include <cstdint>

#include "rt.h"

struct XtGuiWindow;

struct XtWebgpuApi {
  /** Create/find a canvas's WebGPU context; `node` is a `xtgui::Node*`. */
  bool (*canvas_get)(XtGuiWindow *win, const void *node, uint32_t *id);
  /** The existing context id, or 0. */
  bool (*canvas_find)(XtGuiWindow *win, const void *node, uint32_t *id);
  void (*canvas_forget)(XtGuiWindow *win, const void *node);
  void (*shutdown)(XtGuiWindow *win);
  /** Canvas pixel size and the id the paint pipeline keys textures by. */
  void (*info)(uint32_t id, float *width, float *height, uint64_t *canvas_id);
  void (*resize)(uint32_t id, int width, int height);
  /** Wrap a context in a DOM handle; `self` is the canvas element handle. */
  xt_value (*handle)(xt_value self, uint32_t id);
  xt_value (*proto)();
  /** `navigator.gpu` and the `GPU…` host types/namespaces. */
  xt_value (*navigator_gpu)();
  xt_value (*host_type)(const char *name);
  /** The colour texture of a canvas, for the paint pipeline. */
  void *(*color_texture)(uint64_t canvas_id);
};

void xt_webgpu_api_install(const XtWebgpuApi *api);
const XtWebgpuApi *xt_webgpu_api();

#endif /* XT_GUI_WEBGPU_API_H */
