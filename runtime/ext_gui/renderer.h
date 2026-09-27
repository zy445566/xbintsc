/*
 * xbintsc GUI engine — SDL_GPU 2D renderer.
 *
 * A single graphics pipeline draws every shape in a `DisplayList` as triangles:
 * one batched vertex buffer per window, a viewport-size push constant, and a
 * rounded-rectangle distance field in the fragment shader for antialiased
 * fills. The shared pipeline is created lazily from the device's supported
 * shader format; on backends we cannot target yet the window still clears and
 * the geometry is skipped.
 *
 * Only the Metal (MSL) path is implemented so far; Vulkan/D3D12 need SPIR-V /
 * DXIL blobs and are a build-time TODO (see doc/gui.md).
 */
#ifndef XT_GUI_RENDERER_H
#define XT_GUI_RENDERER_H

#include <SDL3/SDL.h>
#include <cstdint>

namespace xtgui {
struct DisplayList;
}

/** Per-window GPU geometry (vertex buffer + draw range). */
struct XtGuiGeometry {
  SDL_GPUBuffer *buffer = nullptr;
  uint32_t capacity_bytes = 0;
  uint32_t vertex_count = 0;
  bool dirty = true;
};

/** Create the shared pipeline if needed. Returns false when the backend has no
 * shader format we support (the caller then skips geometry). */
bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference);
/** Release the shared pipeline (after SDL_WaitForGPUIdle). */
void xt_gui_renderer_destroy(SDL_GPUDevice *device);
/** Release a window's vertex buffer (after SDL_WaitForGPUIdle). */
void xt_gui_geometry_destroy(SDL_GPUDevice *device, XtGuiGeometry *geometry);
/** Upload `list` into `geometry` (own command buffer; call outside a pass). */
void xt_gui_geometry_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                            const xtgui::DisplayList &list);
/** Draw `geometry` inside an active render pass. */
void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                          XtGuiGeometry *geometry, float viewport_width, float viewport_height);

#endif /* XT_GUI_RENDERER_H */
