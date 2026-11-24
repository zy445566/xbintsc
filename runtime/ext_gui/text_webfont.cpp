/*
 * xbintsc GUI engine — WOFF and WOFF2 decompression for author `@font-face`
 * sources.
 *
 * Only `webfont_bridge.c` references `xt_webfont_feature_register`, so this
 * translation unit (and the miniz/Brotli/woff2 members it calls) is extracted
 * from `gui.a` only when the `webfonts` link feature is active.
 *
 * WOFF (W3C WOFF 1.0) is the sfnt table directory with each table individually
 * zlib-compressed, so this unit rebuilds a plain sfnt from the decompressed
 * tables. WOFF2 (W3C WOFF 2.0) stores one Brotli stream of
 * *transformed* tables (glyf/loca and hmtx are re-encoded); reconstructing that
 * is the woff2 reference decoder's job, and its output is already a complete
 * sfnt file.
 */

#include "text_font_bytes.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "miniz_tinfl.h"
#include <woff2/decode.h>

namespace xtgui {
namespace {

constexpr uint32_t tagOf(char a, char b, char c, char d) {
  return ((uint32_t)(unsigned char)a << 24) | ((uint32_t)(unsigned char)b << 16) |
         ((uint32_t)(unsigned char)c << 8) | (uint32_t)(unsigned char)d;
}

constexpr uint32_t kTagWoff = tagOf('w', 'O', 'F', 'F');
constexpr uint32_t kTagWoff2 = tagOf('w', 'O', 'F', '2');

/** Decompression-bomb guard: a real font is far below this. WOFF declares the
 * uncompressed size up front, and the rebuilt table set is checked against the
 * same bound. */
constexpr size_t kMaxSfntSize = 64u * 1024u * 1024u;

uint16_t be16(const unsigned char *data) {
  return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

uint32_t be32(const unsigned char *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
         (uint32_t)data[3];
}

void put16(unsigned char *data, uint16_t value) {
  data[0] = (unsigned char)(value >> 8);
  data[1] = (unsigned char)value;
}

void put32(unsigned char *data, uint32_t value) {
  data[0] = (unsigned char)(value >> 24);
  data[1] = (unsigned char)(value >> 16);
  data[2] = (unsigned char)(value >> 8);
  data[3] = (unsigned char)value;
}

size_t paddedSize(size_t length) { return (length + 3u) & ~(size_t)3u; }

/** One WOFF table directory entry. */
struct WoffTable {
  uint32_t tag = 0;
  uint32_t offset = 0;
  uint32_t compressed_length = 0;
  uint32_t original_length = 0;
  uint32_t checksum = 0;
};

/** Inflate one WOFF table. WOFF compresses each table with zlib (RFC 1950),
 * not a raw DEFLATE stream, so the 2-byte header and Adler-32 trailer are part
 * of the input. */
bool inflateTable(const unsigned char *source, size_t source_length, uint32_t original_length,
                  std::vector<unsigned char> *out) {
  size_t inflated = 0;
  void *heap = tinfl_decompress_mem_to_heap(source, source_length, &inflated,
                                            TINFL_FLAG_PARSE_ZLIB_HEADER);
  if (heap == nullptr) return false;
  const bool ok = inflated == original_length;
  if (ok) out->assign((const unsigned char *)heap, (const unsigned char *)heap + inflated);
  MZ_FREE(heap);
  return ok;
}

/** Rebuild a plain sfnt file from the WOFF table set. Every table keeps the
 * checksum the WOFF directory recorded, and the data is written 4-byte aligned
 * in directory order, which is what an sfnt reader expects. */
bool buildSfnt(uint32_t flavor, const std::vector<WoffTable> &tables,
               const std::vector<std::vector<unsigned char>> &data,
               std::vector<unsigned char> *out) {
  const size_t count = tables.size();
  if (count == 0 || count > 0x0fffu) return false;
  size_t total = 12 + count * 16;
  for (size_t i = 0; i < count; i++) {
    if (data[i].size() != tables[i].original_length) return false;
    total += paddedSize(data[i].size());
    if (total > kMaxSfntSize) return false;
  }

  out->assign(total, 0);
  unsigned char *sfnt = out->data();
  put32(sfnt, flavor);
  put16(sfnt + 4, (uint16_t)count);
  int entry_selector = 0;
  while (((size_t)1 << (entry_selector + 1)) <= count) entry_selector++;
  const uint16_t search_range = (uint16_t)(((size_t)1 << entry_selector) * 16);
  put16(sfnt + 6, search_range);
  put16(sfnt + 8, (uint16_t)entry_selector);
  put16(sfnt + 10, (uint16_t)(count * 16 - search_range));

  size_t offset = 12 + count * 16;
  for (size_t i = 0; i < count; i++) {
    unsigned char *record = sfnt + 12 + i * 16;
    put32(record, tables[i].tag);
    put32(record + 4, tables[i].checksum);
    put32(record + 8, (uint32_t)offset);
    put32(record + 12, tables[i].original_length);
    if (!data[i].empty()) std::memcpy(sfnt + offset, data[i].data(), data[i].size());
    offset += paddedSize(data[i].size());
  }
  return true;
}

bool decodeWoff(const std::vector<unsigned char> &source, std::vector<unsigned char> *out) {
  /* Fixed 44-byte header, then one 20-byte record per table. */
  constexpr size_t kHeaderSize = 44;
  constexpr size_t kRecordSize = 20;
  const unsigned char *bytes = source.data();
  if (source.size() < kHeaderSize) return false;

  const uint32_t flavor = be32(bytes + 4);
  const uint32_t declared_length = be32(bytes + 8);
  const uint16_t count = be16(bytes + 12);
  const uint32_t total_sfnt_size = be32(bytes + 16);
  if (count == 0 || count > 0x0fffu) return false;
  if (total_sfnt_size == 0 || total_sfnt_size > kMaxSfntSize) return false;
  if (declared_length != 0 && declared_length > source.size()) return false;
  if (kHeaderSize + (size_t)count * kRecordSize > source.size()) return false;

  std::vector<WoffTable> tables(count);
  std::vector<std::vector<unsigned char>> data(count);
  for (size_t i = 0; i < count; i++) {
    const unsigned char *record = bytes + kHeaderSize + i * kRecordSize;
    WoffTable &table = tables[i];
    table.tag = be32(record);
    table.offset = be32(record + 4);
    table.compressed_length = be32(record + 8);
    table.original_length = be32(record + 12);
    table.checksum = be32(record + 16);
    if (table.original_length == 0 || table.compressed_length == 0) return false;
    if (table.original_length > kMaxSfntSize) return false;
    /* A table is stored uncompressed when its compressed size is not smaller. */
    if (table.compressed_length > table.original_length) return false;
    if (table.offset > source.size() ||
        table.compressed_length > source.size() - table.offset) {
      return false;
    }
    if (table.compressed_length == table.original_length) {
      data[i].assign(bytes + table.offset, bytes + table.offset + table.compressed_length);
      continue;
    }
    if (!inflateTable(bytes + table.offset, table.compressed_length, table.original_length,
                      &data[i])) {
      return false;
    }
  }
  return buildSfnt(flavor, tables, data, out);
}

bool decodeWoff2(const std::vector<unsigned char> &source, std::vector<unsigned char> *out) {
  if (source.size() < 48) return false;
  std::string decoded;
  woff2::WOFF2StringOut writer(&decoded);
  if (!woff2::ConvertWOFF2ToTTF(source.data(), source.size(), &writer)) return false;
  out->assign(decoded.begin(), decoded.end());
  return true;
}

/** The sfnt version tags a rebuilt container may start with. WOFF/WOFF2 cannot
 * hold a collection, but accepting `ttcf` keeps a decoder bug from turning into
 * a "corrupt font" report when the source really was one. */
bool looksLikeSfnt(const std::vector<unsigned char> &bytes) {
  if (bytes.size() < 4) return false;
  const uint32_t tag = be32(bytes.data());
  return tag == 0x00010000u || tag == tagOf('t', 'r', 'u', 'e') ||
         tag == tagOf('t', 'y', 'p', '1') || tag == tagOf('O', 'T', 'T', 'O') ||
         tag == tagOf('t', 't', 'c', 'f');
}

bool decode(const std::vector<unsigned char> &source, std::vector<unsigned char> *out) {
  if (source.size() < 4) return false;
  const uint32_t tag = be32(source.data());
  if (tag == kTagWoff) {
    if (!decodeWoff(source, out)) return false;
  } else if (tag == kTagWoff2) {
    if (!decodeWoff2(source, out)) return false;
  } else {
    return false;
  }
  return looksLikeSfnt(*out);
}

}  // namespace
}  // namespace xtgui

/** Entry point referenced by `webfont_bridge.c`; it installs the decoder and,
 * through that reference, makes the linker extract this object (and the
 * miniz/Brotli/woff2 members) from `gui.a`. */
extern "C" void xt_webfont_feature_register(void) {
  static const xtgui::WebfontDecoder kDecoder = {xtgui::decode};
  xtgui::xt_font_webfont_install(&kDecoder);
}
