/*
 * xbintsc GUI engine — shared declarations for the renderer translation units
 * (`renderer.cpp`, `renderer_text.cpp`, `renderer_image.cpp`).
 *
 * The GPU state lives in `xtgui::renderer_detail` with external linkage so the
 * three translation units can share it (it used to be an anonymous namespace).
 */
#ifndef XT_GUI_RENDERER_INTERNAL_H
#define XT_GUI_RENDERER_INTERNAL_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "paint.h"
#include "renderer.h"

namespace xtgui {
namespace renderer_detail {

struct Vertex {
  float x, y;
  float lx, ly;
  float hx, hy;
  float radius;
  float r, g, b, a;
};

struct TextVertex {
  float x, y;
  float u, v;
  float r, g, b, a;
};

/* Image quads are batched by texture; textures are cached by source path. */
extern SDL_GPUGraphicsPipeline *g_pipeline;
extern SDL_GPUGraphicsPipeline *g_text_pipeline;
extern SDL_GPUGraphicsPipeline *g_image_pipeline;
extern SDL_GPUSampler *g_sampler;
extern int g_renderer_failed;
extern bool g_text_ok;
extern std::unordered_map<std::string, SDL_GPUTexture *> g_image_textures;

/* -- glyph atlas ---------------------------------------------------------- */

constexpr int kAtlasSize = 2048;

struct GlyphEntry {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  int width = 0, height = 0, left = 0, top = 0;
};

struct GlyphKey {
  const void *font = nullptr;
  uint32_t glyph = 0;
  bool operator==(const GlyphKey &other) const {
    return font == other.font && glyph == other.glyph;
  }
};

struct GlyphKeyHash {
  size_t operator()(const GlyphKey &key) const {
    return std::hash<const void *>()(key.font) * 1099511628211ull ^
           (size_t)(key.glyph * 2654435761u);
  }
};

extern SDL_GPUTexture *g_atlas_texture;
extern SDL_GPUDevice *g_device;
extern SDL_GPUTextureFormat g_swapchain_format;
extern std::vector<unsigned char> g_atlas_mirror;
extern std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> g_glyphs;
extern int g_atlas_x;
extern int g_atlas_y;
extern int g_atlas_row_height;
extern bool g_atlas_dirty;
extern int g_atlas_dirty_x0, g_atlas_dirty_y0, g_atlas_dirty_x1, g_atlas_dirty_y1;
extern bool g_atlas_full_logged;

/* -- geometry helpers ----------------------------------------------------- */

/** Reused vertex staging arrays (one frame at a time, cleared on each upload).
 * Keeping their capacity avoids reallocating a few megabytes per repaint. */
extern std::vector<Vertex> g_shape_staging;
extern std::vector<TextVertex> g_text_staging;
extern std::vector<TextVertex> g_image_staging;

/** The command buffer an upload records into: the frame's batch when given,
 * otherwise a freshly acquired one that `uploadDone` submits. */
SDL_GPUCommandBuffer *uploadCommand(SDL_GPUDevice *device, XtGuiUploadBatch *batch);
/** Submit a command buffer acquired for a one-off upload (`batch == nullptr`). */
void uploadDone(SDL_GPUCommandBuffer *cmd, XtGuiUploadBatch *batch);

/** Push one rounded rectangle, clipped by the op's `clip-path` polygons (if
 * any). Returns the number of vertices appended. */
uint32_t pushShape(std::vector<Vertex> &verts, const xtgui::PaintRect &rect,
                   const xtgui::PaintClip &clip, const xtgui::DisplayList &list);
/** Push one glyph quad, clipped by `clip-path`. */
uint32_t pushGlyph(std::vector<TextVertex> &verts, float x, float y, float width, float height,
                   const GlyphEntry &entry, const xtgui::Color &color,
                   const xtgui::Matrix2D &transform, const xtgui::PaintClip &clip,
                   const xtgui::DisplayList &list);
/** Push a textured quad, applying a `transform` matrix to its four corners and
 * clipping it by `clip-path`. */
uint32_t pushQuad(std::vector<TextVertex> &verts, const float corners_x[4],
                  const float corners_y[4], const float u[4], const float v[4],
                  const xtgui::Color &color, const xtgui::Matrix2D &transform,
                  const xtgui::PaintClip &clip, const xtgui::DisplayList &list);
void uploadVertices(SDL_GPUDevice *device, XtGuiGeometry *geometry, const void *data,
                    uint32_t count, uint32_t stride, XtGuiUploadBatch *batch);

/* -- text ----------------------------------------------------------------- */

bool ensureAtlasTexture(SDL_GPUDevice *device);
void markAtlasDirty(int x, int y, int width, int height);
bool ensureGlyph(SDL_GPUDevice *device, xtgui::Font *font, uint32_t glyph, GlyphEntry &out);
void flushAtlas(SDL_GPUDevice *device, XtGuiUploadBatch *batch);
SDL_GPUGraphicsPipeline *createTextPipeline(SDL_GPUDevice *device, SDL_Window *reference);
SDL_GPUSampler *createSampler(SDL_GPUDevice *device);

/* -- draw runs ------------------------------------------------------------ */

/** Draw the drawable runs in `[begin, end)` into an active pass. Layer markers
 * are skipped. State is re-bound after a pass restart. */
void drawRuns(SDL_GPURenderPass *pass, const std::vector<XtGuiDrawRun> &runs, size_t begin,
              size_t end, XtGuiGeometry *shapes, XtGuiGeometry *texts, XtGuiGeometry *images,
              int target_width, int target_height, float pixel_density);

/* -- mix-blend-mode layers ------------------------------------------------ */

/** True when the runtime blend pipeline was created (and the backend consumes
 * SPIR-V or MSL). D3D12/DXIL has no shader compiler here, so it falls back to
 * the ordinary single-pass painter. */
bool layerRendererReady(SDL_GPUDevice *device);
/** True when the `backdrop-filter` snapshot/filter pipeline was created. */
bool backdropRendererReady(SDL_GPUDevice *device);
/** Filter `source` into the active scene pass inside the element's rounded
 * border box. No-op when the pipeline is unavailable. */
void drawBackdropPass(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                      SDL_GPUTexture *source, const xtgui::DisplayList &list,
                      const XtGuiDrawRun &run, int target_width, int target_height,
                      float pixel_density);
/** Release the backdrop pipeline and sampler. */
void backdropRendererDestroy(SDL_GPUDevice *device);
/** Render `list` through the offscreen layer compositor into `swapchain`.
 * Returns false before recording any pass when the scene textures cannot be
 * created, so the caller can fall back to the ordinary painter. */
bool drawLayeredScene(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *swapchain,
                      const float clear_color[4], XtGuiGeometry *shapes, XtGuiGeometry *texts,
                      XtGuiGeometry *images, const xtgui::DisplayList &list,
                      float viewport_width, float viewport_height, float pixel_density);
/** Release the blend pipeline and sampler. */
void layerRendererDestroy(SDL_GPUDevice *device);

/* -- images --------------------------------------------------------------- */

SDL_GPUGraphicsPipeline *createImagePipeline(SDL_GPUDevice *device, SDL_Window *reference);
/** Create an RGBA texture holding `image` and upload its pixels. Returns null
 * (after reporting the SDL error) when either step fails. */
SDL_GPUTexture *createImageTexture(SDL_GPUDevice *device, const xtgui::Image &image,
                                   XtGuiUploadBatch *batch);
/** Decode `src` (cached) and upload it as an RGBA texture. `pixel_width` /
 * `pixel_height` are the drawn size in device pixels; vector sources (SVG) are
 * rasterised at that size, raster sources ignore it. The texture cache is keyed
 * by the resulting pixel size, so an SVG drawn at two sizes gets two textures. */
SDL_GPUTexture *ensureImageTexture(SDL_GPUDevice *device, const std::string &src, int pixel_width,
                                   int pixel_height, XtGuiUploadBatch *batch);

/* -- CPU-rasterised shadows ---------------------------------------------- */

/** Upload (once) the blurred mask of a `box-shadow`/`text-shadow` layer and
 * return its cached texture. Bracket the frame's image upload with
 * `beginBitmapUpload`/`finishBitmapUpload` so stale masks are reclaimed. */
SDL_GPUTexture *ensureBitmapTexture(SDL_GPUDevice *device, const xtgui::PaintBitmap &bitmap,
                                    XtGuiUploadBatch *batch);
void beginBitmapUpload();
void finishBitmapUpload(SDL_GPUDevice *device);
void releaseBitmapTextures(SDL_GPUDevice *device);

/* -- background gradients ------------------------------------------------- */

/** Rasterise a `PaintImage` radial gradient into an RGBA texture and cache it by
 * the paint pass's key. The ramp is resolved per device pixel, so a translucent
 * gradient composites once instead of once per band. Bracket a frame's uploads
 * with `beginGradientUpload`/`finishGradientUpload` so textures the frame no
 * longer draws can be reclaimed; returns null when it cannot be created. */
SDL_GPUTexture *ensureGradientTexture(SDL_GPUDevice *device, const xtgui::PaintImage &image,
                                      int pixel_width, int pixel_height, XtGuiUploadBatch *batch);
/** Mark every cached gradient unused; `ensureGradientTexture` marks the ones the
 * upload pass draws. */
void beginGradientUpload();
/** Release cached gradients the pass did not draw, keeping the cache bounded. */
void finishGradientUpload(SDL_GPUDevice *device);
/** Release every rasterised gradient texture. */
void releaseGradientTextures(SDL_GPUDevice *device);

}  // namespace renderer_detail
}  // namespace xtgui

#endif /* XT_GUI_RENDERER_INTERNAL_H */
