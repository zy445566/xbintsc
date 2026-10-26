/*
 * xbintsc GUI engine — `ImageBitmap` pixel storage.
 */

#include "image_bitmap.h"

#include <unordered_map>

namespace xtgui {
namespace {

std::unordered_map<uint64_t, Image> g_bitmaps;
uint64_t g_next_bitmap = 1;

}  // namespace

uint64_t xt_image_bitmap_add(const Image &image) {
  const uint64_t id = g_next_bitmap++;
  g_bitmaps[id] = image;
  return id;
}

const Image *xt_image_bitmap_get(uint64_t id) {
  auto found = g_bitmaps.find(id);
  return found == g_bitmaps.end() ? nullptr : &found->second;
}

}  // namespace xtgui
