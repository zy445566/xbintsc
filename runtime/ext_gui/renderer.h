/*
 * xbintsc GUI engine — SDL_GPU 2D renderer.
 *
 * Three graphics pipelines share one device:
 *   - a shape pipeline that draws every `PaintRect` as triangles, using a
 *     rounded-rectangle distance field for antialiased fills;
 *   - a text pipeline that draws glyphs as textured quads from a shared
 *     grayscale glyph atlas (FreeType rasterises, this file packs/upload);
 *   - an image pipeline that draws `PaintImage` quads sampling per-file RGBA
 *     textures decoded by `image.*`.
 *
 * Geometry is batched into one vertex buffer per window and one draw call per
 * list. Pipelines are created lazily from the device's supported shader format;
 * on backends we cannot target yet the window still clears and geometry is
 * skipped.
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

/** Create the shared pipelines if needed. Returns false when the backend has no
 * shader format we support (the caller then skips geometry). */
bool xt_gui_renderer_ensure(SDL_GPUDevice *device, SDL_Window *reference);
/** Release the shared pipelines and the glyph atlas (after SDL_WaitForGPUIdle). */
void xt_gui_renderer_destroy(SDL_GPUDevice *device);
/** Release a window's vertex buffer (after SDL_WaitForGPUIdle). */
void xt_gui_geometry_destroy(SDL_GPUDevice *device, XtGuiGeometry *geometry);
/** Upload the rectangle geometry from `list` (own command buffer). */
void xt_gui_geometry_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                            const xtgui::DisplayList &list);
/** Draw `geometry` inside an active render pass. */
void xt_gui_renderer_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                          XtGuiGeometry *geometry, float viewport_width, float viewport_height);
/** Shape `list.texts`, ensure their glyphs are in the atlas, and upload the
 * text geometry. `scale` is the device pixel density (1.0 = logical pixels). */
void xt_gui_text_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                        const xtgui::DisplayList &list, float scale);
/** Draw the text geometry (glyph atlas bound) inside an active render pass. */
void xt_gui_text_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, XtGuiGeometry *geometry,
                      float viewport_width, float viewport_height);
/** Decode/upload `list.images` and build the image quad geometry (one batch per
 * texture). Textures are cached by source path. */
void xt_gui_image_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                         const xtgui::DisplayList &list);
/** Draw the image geometry (binds each batch's texture) inside a render pass. */
void xt_gui_image_draw(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass, XtGuiGeometry *geometry,
                       float viewport_width, float viewport_height);
/** Release the image texture cache (after SDL_WaitForGPUIdle). */
void xt_gui_image_textures_destroy(SDL_GPUDevice *device);

#endif /* XT_GUI_RENDERER_H */
