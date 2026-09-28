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
struct ImageBatch {
  SDL_GPUTexture *texture = nullptr;
  uint32_t first = 0;
  uint32_t count = 0;
};

extern SDL_GPUGraphicsPipeline *g_pipeline;
extern SDL_GPUGraphicsPipeline *g_text_pipeline;
extern SDL_GPUGraphicsPipeline *g_image_pipeline;
extern SDL_GPUSampler *g_sampler;
extern int g_renderer_failed;
extern bool g_text_ok;
extern std::vector<ImageBatch> g_image_batches;
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
extern std::vector<unsigned char> g_atlas_mirror;
extern std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> g_glyphs;
extern int g_atlas_x;
extern int g_atlas_y;
extern int g_atlas_row_height;
extern bool g_atlas_dirty;
extern int g_atlas_dirty_x0, g_atlas_dirty_y0, g_atlas_dirty_x1, g_atlas_dirty_y1;
extern bool g_atlas_full_logged;

/* -- geometry helpers ----------------------------------------------------- */

void pushRect(std::vector<Vertex> &verts, const xtgui::PaintRect &rect);
void pushGlyph(std::vector<TextVertex> &verts, float x, float y, float width, float height,
               const GlyphEntry &entry, const xtgui::Color &color);
void uploadVertices(SDL_GPUDevice *device, XtGuiGeometry *geometry, const void *data,
                    uint32_t count, uint32_t stride);

/* -- text ----------------------------------------------------------------- */

bool ensureAtlasTexture(SDL_GPUDevice *device);
void markAtlasDirty(int x, int y, int width, int height);
bool ensureGlyph(SDL_GPUDevice *device, xtgui::Font *font, uint32_t glyph, GlyphEntry &out);
void flushAtlas(SDL_GPUDevice *device);
SDL_GPUGraphicsPipeline *createTextPipeline(SDL_GPUDevice *device, SDL_Window *reference);
SDL_GPUSampler *createSampler(SDL_GPUDevice *device);

/* -- images --------------------------------------------------------------- */

SDL_GPUGraphicsPipeline *createImagePipeline(SDL_GPUDevice *device, SDL_Window *reference);
SDL_GPUTexture *ensureImageTexture(SDL_GPUDevice *device, const std::string &src);

}  // namespace renderer_detail
}  // namespace xtgui

#endif /* XT_GUI_RENDERER_INTERNAL_H */
