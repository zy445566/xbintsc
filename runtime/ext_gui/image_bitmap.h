/*
 * xbintsc GUI engine — `ImageBitmap` storage.
 *
 * `createImageBitmap(source)` returns a settled promise of an immutable
 * bitmap. The pixels stay in C++ so `texImage2D` can consume them without
 * materialising a JavaScript array; the handle object carries the numeric id
 * under `__xt_bitmap`.
 */
#ifndef XT_GUI_IMAGE_BITMAP_H
#define XT_GUI_IMAGE_BITMAP_H

#include <cstdint>

#include "image.h"

namespace xtgui {

/** Store a decoded bitmap and return its process-wide id. */
uint64_t xt_image_bitmap_add(const Image &image);

/** Look up a bitmap; null when the id is unknown. */
const Image *xt_image_bitmap_get(uint64_t id);

}  // namespace xtgui

#endif /* XT_GUI_IMAGE_BITMAP_H */
