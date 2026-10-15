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
#include <vector>

namespace xtgui {
struct DisplayList;
}

/** One draw call in painter order: a contiguous range of vertices in one of the
 * three pipelines, with the clip rectangle (logical pixels) that bounds it. A
 * `clip_width <= 0` means the whole viewport. For image runs, `texture` is the
 * bound texture. */
struct XtGuiDrawRun {
  enum Pipeline { Shape = 0, Text = 1, Image = 2 };
  int pipeline = Shape;
  uint32_t first = 0;
  uint32_t count = 0;
  float clip_x = 0;
  float clip_y = 0;
  float clip_width = 0;
  float clip_height = 0;
  SDL_GPUTexture *texture = nullptr;
};

/** Per-window GPU geometry (vertex buffer + draw range). */
struct XtGuiGeometry {
  SDL_GPUBuffer *buffer = nullptr;
  uint32_t capacity_bytes = 0;
  uint32_t vertex_count = 0;
  bool dirty = true;
  /** Vertex count contributed by each shape of this pipeline's kind, aligned
   * with the display list's payload vector (used to build the run list). */
  std::vector<uint32_t> op_counts;
  /** Texture for each image op, aligned with `DisplayList::images` (image
   * geometry only). */
  std::vector<SDL_GPUTexture *> op_textures;
  /** Painter-order draw runs, built from the display list by
   * `xt_gui_build_runs` and consumed by `xt_gui_renderer_draw`. */
  std::vector<XtGuiDrawRun> runs;
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
                          XtGuiGeometry *shapes, XtGuiGeometry *texts, XtGuiGeometry *images,
                          float viewport_width, float viewport_height, float pixel_density);
/** Shape `list.texts`, ensure their glyphs are in the atlas, and upload the
 * text geometry. `scale` is the device pixel density (1.0 = logical pixels). */
void xt_gui_text_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                        const xtgui::DisplayList &list, float scale);
/** Decode/upload `list.images` and build the image quad geometry (one batch per
 * texture). Textures are cached by source path and pixel size. `scale` is the
 * device pixel density (1.0 = logical pixels) used to pick the rasterisation
 * size of a vector source (SVG); raster sources decode at their intrinsic size. */
void xt_gui_image_upload(SDL_GPUDevice *device, XtGuiGeometry *geometry,
                         const xtgui::DisplayList &list, float scale);
/** Release the image texture cache (after SDL_WaitForGPUIdle). */
void xt_gui_image_textures_destroy(SDL_GPUDevice *device);
/** Build the painter-order run list from the display list, using the per-op
 * vertex counts and textures recorded by the three upload functions. Stores the
 * result in `shapes->runs`. */
void xt_gui_build_runs(XtGuiGeometry *shapes, XtGuiGeometry *texts, XtGuiGeometry *images,
                       const xtgui::DisplayList &list);
/** Re-upload the current decoded frame of every live `<video>` texture. Called
 * once per frame, before the render command buffer, so the texture sampled by
 * the draw already holds that frame. */
void xt_gui_media_upload_frames(SDL_GPUDevice *device);
/** Release one media texture (when a `<video>` is removed or navigated away). */
void xt_gui_media_texture_release(SDL_GPUDevice *device, uint64_t media_id);
/** Re-upload the current bitmap of every live `<canvas>` texture. Called once
 * per frame, before the render command buffer, so a canvas animated from
 * `requestAnimationFrame` reaches the screen without a relayout. */
void xt_gui_canvas_upload_frames(SDL_GPUDevice *device);
/** Release one canvas texture (when the canvas is dropped). */
void xt_gui_canvas_texture_release(SDL_GPUDevice *device, uint64_t canvas_id);

#endif /* XT_GUI_RENDERER_H */
