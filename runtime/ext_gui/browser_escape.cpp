/*
 * xbintsc GUI engine — Annex-B `escape`.
 *
 * `escape` is not in any modern standard, only in Annex B of ECMA-262, but it
 * survives in one very common idiom: `decodeURIComponent(escape(binary))`,
 * which turns a string of raw bytes (one code unit per byte) back into text.
 * Three.js uses exactly that to decode its binary payloads.
 *
 * The mapping is per *UTF-16 code unit*: a unit below 0x100 becomes `%XX`, any
 * other unit `%uXXXX`, and an astral code point becomes a surrogate pair of
 * `%uXXXX` escapes. The runtime stores UTF-8, so this decodes one code point at
 * a time and emits its units; a string that came from bytes round-trips
 * unchanged.
 */

#include "gui_engine.h"

#include <cstdio>
#include <string>

namespace {

const char *cstr(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != NULL ? data : "";
}

/** Decode one UTF-8 code point at `index`, advancing past it. */
unsigned int next_code_point(const std::string &text, size_t &index) {
  unsigned char lead = (unsigned char)text[index];
  if (lead < 0x80) {
    index += 1;
    return lead;
  }
  size_t extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
  if (extra == 0 || index + extra >= text.size()) {
    index += 1;
    return lead;
  }
  unsigned int code = lead & (0xFF >> (extra + 2));
  for (size_t step = 1; step <= extra; step++) {
    code = (code << 6) | ((unsigned char)text[index + step] & 0x3F);
  }
  index += extra + 1;
  return code;
}

}  // namespace

/** Annex-B `escape`: every UTF-16 code unit becomes `%XX` while it fits in a
 * byte and `%uXXXX` above that, so `decodeURIComponent(escape(binary))` decodes
 * a byte string (the idiom Three.js uses to turn a binary string into text). */
extern "C" xt_value xt_browser_escape(int32_t argc, xt_value *argv) {
  std::string text = cstr(xt_arg(argc, argv, 0));
  std::string out;
  char buffer[16];
  for (size_t index = 0; index < text.size();) {
    unsigned int code = next_code_point(text, index);
    if (code >= 0x10000) {
      unsigned int rest = code - 0x10000;
      snprintf(buffer, sizeof buffer, "%%u%04X%%u%04X", 0xD800 + (rest >> 10),
               0xDC00 + (rest & 0x3FF));
      out += buffer;
    } else if (code < 0x100) {
      /* Unreserved characters stay literal, exactly as the spec lists them. */
      char byte = (char)code;
      bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                   (byte >= '0' && byte <= '9') || byte == '@' || byte == '*' || byte == '_' ||
                   byte == '+' || byte == '-' || byte == '.' || byte == '/';
      if (plain) {
        out.push_back(byte);
      } else {
        snprintf(buffer, sizeof buffer, "%%%02X", code);
        out += buffer;
      }
    } else {
      snprintf(buffer, sizeof buffer, "%%u%04X", code);
      out += buffer;
    }
  }
  return xt_string_from_cstr(out.c_str());
}



