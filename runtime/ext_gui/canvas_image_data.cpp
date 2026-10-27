/*
 * xbintsc GUI engine — `putImageData` and PNG export for `<canvas>`.
 *
 * `putImageData` writes RGBA bytes straight into the bitmap: it ignores the
 * transform, `globalAlpha` and compositing, exactly as the canvas API requires.
 * PNG export writes the bitmap back out for `toDataURL`/`toBlob`; it uses a
 * zlib "stored" stream (no compression) so the engine needs no deflate
 * dependency, which is enough for the small UI snapshots pages take.
 */

#include "canvas.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace xtgui {

void Canvas2D::set_image_data(const std::vector<unsigned char> &rgba, int data_width,
                              int data_height, int dx, int dy, int dirty_x, int dirty_y,
                              int dirty_width, int dirty_height) {
  if (data_width <= 0 || data_height <= 0) return;
  if (rgba.size() < (size_t)data_width * (size_t)data_height * 4) return;
  int source_x0 = 0;
  int source_y0 = 0;
  int source_x1 = data_width;
  int source_y1 = data_height;
  /* The optional dirty rectangle is in image coordinates. */
  if (dirty_width > 0 && dirty_height > 0) {
    source_x0 = std::max(0, dirty_x);
    source_y0 = std::max(0, dirty_y);
    source_x1 = std::min(data_width, dirty_x + dirty_width);
    source_y1 = std::min(data_height, dirty_y + dirty_height);
  }
  if (source_x1 <= source_x0 || source_y1 <= source_y0) return;
  int dest_x0 = std::max(0, dx + source_x0);
  int dest_y0 = std::max(0, dy + source_y0);
  int dest_x1 = std::min(image_.width, dx + source_x1);
  int dest_y1 = std::min(image_.height, dy + source_y1);
  for (int py = dest_y0; py < dest_y1; py++) {
    int source_row = py - dy;
    const unsigned char *src =
        rgba.data() + ((size_t)source_row * (size_t)data_width + (size_t)(dest_x0 - dx)) * 4;
    unsigned char *dst =
        image_.pixels.data() + ((size_t)py * (size_t)image_.width + (size_t)dest_x0) * 4;
    memcpy(dst, src, (size_t)(dest_x1 - dest_x0) * 4);
  }
  generation_++;
}

namespace {

uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
  }
  return ~crc;
}

void append_u32(std::vector<unsigned char> *out, uint32_t value) {
  out->push_back((unsigned char)(value >> 24));
  out->push_back((unsigned char)(value >> 16));
  out->push_back((unsigned char)(value >> 8));
  out->push_back((unsigned char)value);
}

void append_chunk(std::vector<unsigned char> *out, const char type[4],
                  const std::vector<unsigned char> &data) {
  append_u32(out, (uint32_t)data.size());
  size_t crc_start = out->size();
  out->insert(out->end(), type, type + 4);
  out->insert(out->end(), data.begin(), data.end());
  uint32_t crc = crc32_update(0, out->data() + crc_start, out->size() - crc_start);
  append_u32(out, crc);
}

uint32_t adler32(const std::vector<unsigned char> &data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (unsigned char byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

/** zlib stream with uncompressed (stored) deflate blocks. */
std::vector<unsigned char> zlib_store(const std::vector<unsigned char> &data) {
  std::vector<unsigned char> out;
  out.push_back(0x78);
  out.push_back(0x01);
  size_t offset = 0;
  do {
    size_t chunk = std::min<size_t>(65535, data.size() - offset);
    bool last = offset + chunk >= data.size();
    out.push_back(last ? 1 : 0);
    out.push_back((unsigned char)(chunk & 0xFF));
    out.push_back((unsigned char)(chunk >> 8));
    out.push_back((unsigned char)(~chunk & 0xFF));
    out.push_back((unsigned char)((~chunk >> 8) & 0xFF));
    out.insert(out.end(), data.begin() + offset, data.begin() + offset + chunk);
    offset += chunk;
  } while (offset < data.size());
  append_u32(&out, adler32(data));
  return out;
}

}  // namespace

bool Canvas2D::encode_png(std::vector<unsigned char> *out) const {
  if (out == nullptr || image_.width <= 0 || image_.height <= 0) return false;
  out->clear();
  static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out->insert(out->end(), signature, signature + 8);

  std::vector<unsigned char> header;
  append_u32(&header, (uint32_t)image_.width);
  append_u32(&header, (uint32_t)image_.height);
  header.push_back(8);  // bit depth
  header.push_back(6);  // colour type: RGBA
  header.push_back(0);  // compression
  header.push_back(0);  // filter
  header.push_back(0);  // interlace
  append_chunk(out, "IHDR", header);

  std::vector<unsigned char> raw;
  raw.reserve(((size_t)image_.width * 4 + 1) * (size_t)image_.height);
  for (int y = 0; y < image_.height; y++) {
    raw.push_back(0);  // filter type: none
    const unsigned char *row = image_.pixels.data() + (size_t)y * (size_t)image_.width * 4;
    raw.insert(raw.end(), row, row + (size_t)image_.width * 4);
  }
  append_chunk(out, "IDAT", zlib_store(raw));
  append_chunk(out, "IEND", std::vector<unsigned char>());
  return true;
}

}  // namespace xtgui
