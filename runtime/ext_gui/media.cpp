/*
 * xbintsc GUI engine — media decoder dispatch.
 *
 * The engine models `<video>`/`<audio>` elements, but the decoder is an
 * optional backend supplied by the separate `media` extension. That backend
 * installs its table through `xt_media_install` from a bridge object that is
 * linked directly when the extension is enabled (see
 * `runtime/ext_media/media_bridge.c`), so this translation unit stays tiny and
 * FFmpeg-free.
 *
 * A program built with `--ext gui` alone leaves `g_api` null. `MediaPlayer::open`
 * then fails with an actionable message and the element fires `error`, exactly
 * as it does for an unreadable file. Nothing here touches GUI state, so it can
 * be compiled independently of SDL and the renderer.
 */

#include "media.h"

#include <cstdio>
#include <cstring>

namespace {

const XtMediaApi* g_api = nullptr;

/** Message length used for a backend's open failure. */
constexpr size_t kErrorSize = 256;

}  // namespace

namespace xtgui {

std::unique_ptr<MediaPlayer> MediaPlayer::open(const std::string& path, std::string* error,
                                               std::unique_ptr<AudioSink> audio) {
  if (g_api == nullptr || g_api->open == nullptr) {
    if (error) *error = "media support requires the 'media' extension (pass --ext media)";
    return nullptr;
  }
  char message[kErrorSize] = {0};
  auto* player = static_cast<MediaPlayer*>(
      g_api->open(path.c_str(), message, sizeof(message), audio.release()));
  if (player == nullptr && error != nullptr) {
    *error = message[0] != '\0' ? message : "cannot open media: " + path;
  }
  return std::unique_ptr<MediaPlayer>(player);
}

MediaPlayer* xt_media_by_id(uint64_t id) {
  return g_api != nullptr && g_api->by_id != nullptr
             ? static_cast<MediaPlayer*>(g_api->by_id(id))
             : nullptr;
}

void xt_media_shutdown() {
  if (g_api != nullptr && g_api->shutdown != nullptr) g_api->shutdown();
}

}  // namespace xtgui

extern "C" void xt_media_install(const XtMediaApi* api) { g_api = api; }
