/*
 * xbintsc GUI engine — font file bytes shared by the metadata reader and both
 * text backends.
 *
 * A `.ttf`/`.ttc`/`.otf`/`.otc` file is already sfnt, so its bytes are handed
 * out as read. A `.woff`/`.woff2` file stores the same tables deflated (WOFF) or
 * Brotli-compressed behind a transformed table set (WOFF2), so it is decoded to
 * a plain sfnt by the optional `webfonts` link feature before anyone parses it.
 * The decoder is a function table installed by `webfont_bridge.c`:
 * `text_webfont.cpp` is the only place that references miniz/Brotli/woff2, and
 * without the bridge those members are never extracted from `gui.a`.
 *
 * Every path is decoded at most once per process: the font database parses the
 * same bytes for its CSS metadata that the selected backend loads for outlines.
 */
#ifndef XT_GUI_TEXT_FONT_BYTES_H
#define XT_GUI_TEXT_FONT_BYTES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xtgui {

/** The two compressed container signatures (`'wOFF'`, `'wOF2'`). */
constexpr uint32_t kWebfontWoffTag = 0x774f4646u;
constexpr uint32_t kWebfontWoff2Tag = 0x774f4632u;

/** The WOFF/WOFF2 decompressor installed by the `webfonts` link feature. */
struct WebfontDecoder {
  /** Decode a compressed font file into a plain sfnt file in `out`. */
  bool (*decode)(const std::vector<unsigned char> &source,
                 std::vector<unsigned char> *out);
};

/** Install the decoder. Called by `webfont_bridge.c`'s constructor; a program
 * built without the feature leaves it null and compressed sources are skipped
 * with a diagnostic. */
void xt_font_webfont_install(const WebfontDecoder *decoder);

/** True when a WOFF/WOFF2 decoder is installed (the `webfonts` feature is
 * linked). Font-directory scans consult it so an undecodable file is not even
 * indexed, and the compressed-source diagnostic stays with the author rule. */
bool xt_font_webfont_available();

/** True when `path` is a WOFF/WOFF2 file. A backend that can stream an sfnt file
 * (FreeType and HarfBuzz both can) uses this to keep a large system font out of
 * memory, while a compressed source always has to be decoded into a buffer. */
bool xt_font_file_is_compressed(const std::string &path);

/** The uncompressed sfnt bytes for `path`, decoded once per process. Returns
 * null when the file cannot be read or a compressed container cannot be
 * decoded; the failure is cached too, so a missing source is reported once. */
std::shared_ptr<const std::vector<unsigned char>> xt_font_file_bytes(const std::string &path);

/** True when `data` starts with a WOFF/WOFF2 signature. */
bool xt_font_is_webfont(const unsigned char *data, size_t size);

/** Drop the cached bytes (after the text backends released their faces). */
void xt_font_bytes_shutdown();

}  // namespace xtgui

#endif /* XT_GUI_TEXT_FONT_BYTES_H */
