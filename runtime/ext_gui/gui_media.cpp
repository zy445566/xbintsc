/*
 * xbintsc GUI engine — `<video>` / `<audio>` lifecycle.
 *
 * This unit bridges three pieces:
 *   - the DOM (`Node`) and the per-window media table on `XtGuiWindow`;
 *   - the optional decoders the `media` extension installs (the engine's
 *     `media.cpp` dispatch; FFmpeg itself lives in `runtime/ext_media/`);
 *   - the renderer (`renderer_image.cpp`), which owns the GPU textures.
 *
 * It also provides the SDL audio sink the decoder writes PCM to, which keeps
 * SDL out of `media.a`.
 *
 * The frame loop calls `xt_gui_media_sync` (open/close decoders so the next
 * layout sees the current sources and intrinsic sizes) and `xt_gui_media_tick`
 * (decode for the elapsed wall clock and fire the resulting DOM events) once
 * per frame. No `xt_value` is stored here, so there is nothing for the
 * collector to trace.
 */

#include "gui_internal.h"

#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

/** Audio format the decoder resamples everything to. */
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

/** Initialise SDL's audio subsystem once, best effort: a headless CI box has
 * no device, and video still plays without sound. */
bool audioSubsystemReady() {
  static const bool ready = SDL_InitSubSystem(SDL_INIT_AUDIO);
  return ready;
}

/** SDL push-mode audio output for one decoder.
 *
 * There is no audio callback: the decoder writes PCM on the main thread and SDL
 * pulls from the queued buffer on its own thread. `queued_seconds()` lets the
 * decoder keep that buffer shallow. */
class SdlAudioSink final : public xtgui::AudioSink {
 public:
  ~SdlAudioSink() override {
    if (stream_ != nullptr) SDL_DestroyAudioStream(stream_);
  }

  bool open() override {
    if (tried_) return stream_ != nullptr;
    tried_ = true;
    if (!audioSubsystemReady()) return false;
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = kAudioChannels;
    spec.freq = kAudioRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_ == nullptr) return false;
    SDL_PauseAudioStreamDevice(stream_);
    return true;
  }

  void write(const void *data, size_t bytes) override {
    if (stream_ != nullptr) SDL_PutAudioStreamData(stream_, data, static_cast<int>(bytes));
  }
  void clear() override {
    if (stream_ != nullptr) SDL_ClearAudioStream(stream_);
  }
  void pause() override {
    if (stream_ != nullptr) SDL_PauseAudioStreamDevice(stream_);
  }
  void resume() override {
    if (stream_ != nullptr) SDL_ResumeAudioStreamDevice(stream_);
  }
  void set_gain(float gain) override {
    if (stream_ != nullptr) SDL_SetAudioStreamGain(stream_, gain);
  }
  double queued_seconds() const override {
    if (stream_ == nullptr) return 0.0;
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    return bytes / static_cast<double>(kAudioRate * kAudioChannels * sizeof(float));
  }

 private:
  SDL_AudioStream *stream_ = nullptr;
  bool tried_ = false;
};

bool isMediaElement(const xtgui::Node *node) {
  return node != nullptr && node->isElement() && (node->isTag("video") || node->isTag("audio"));
}

/** The element's `src`, or the `src` of its first child `<source>` (which is
 * how a `<video>` with fallback formats is written). */
std::string sourceOf(const xtgui::Node *node) {
  if (!isMediaElement(node)) return "";
  const std::string *src = node->attr("src");
  if (src != nullptr && !src->empty()) return *src;
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    if (child->isElement() && child->isTag("source")) {
      const std::string *child_src = child->attr("src");
      if (child_src != nullptr && !child_src->empty()) return *child_src;
    }
  }
  return "";
}

bool booleanAttr(const xtgui::Node *node, const char *name) {
  return node->attr(name) != nullptr;
}

void collectMedia(const xtgui::Node *node, std::vector<const xtgui::Node *> *out) {
  if (node == nullptr) return;
  if (isMediaElement(node)) {
    out->push_back(node);
    return;  // a media element's children are `<source>`/`<track>`, not media
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) collectMedia(child.get(), out);
}

void releasePlayerTexture(const xtgui::MediaPlayer *player) {
  if (player != nullptr && guidev::g_device != nullptr) {
    xt_gui_media_texture_release(guidev::g_device, player->id());
  }
}

/** Open a decoder for `node`/`src`, apply its boolean attributes, record it and
 * return it (or NULL, after firing `error`). */
xtgui::MediaPlayer *openEntry(XtGuiWindow *win, const xtgui::Node *node, const std::string &src) {
  XtGuiMediaEntry entry;
  entry.src = src;
  std::string error;
  entry.player = xtgui::MediaPlayer::open(src, &error, std::make_unique<SdlAudioSink>());
  if (!entry.player) {
    entry.failed = true;
    win->media_players[node] = std::move(entry);
    xt_gui_dispatch_to_node(win, node, "error");
    return nullptr;
  }
  entry.player->set_loop(booleanAttr(node, "loop"));
  entry.player->set_muted(booleanAttr(node, "muted"));
  entry.player->set_autoplay(booleanAttr(node, "autoplay"));
  xtgui::MediaPlayer *raw = entry.player.get();
  win->media_players[node] = std::move(entry);
  return raw;
}

void emitMediaEvents(XtGuiWindow *win, const xtgui::Node *node, unsigned events) {
  if (events == 0) return;
  if (events & xtgui::kMediaEventLoadedMetadata) {
    xt_gui_dispatch_to_node(win, node, "loadedmetadata");
  }
  if (events & xtgui::kMediaEventPlay) xt_gui_dispatch_to_node(win, node, "play");
  if (events & xtgui::kMediaEventPause) xt_gui_dispatch_to_node(win, node, "pause");
  if (events & xtgui::kMediaEventEnded) xt_gui_dispatch_to_node(win, node, "ended");
  if (events & xtgui::kMediaEventTimeUpdate) xt_gui_dispatch_to_node(win, node, "timeupdate");
  if (events & xtgui::kMediaEventError) xt_gui_dispatch_to_node(win, node, "error");
}

}  // namespace

xtgui::MediaPlayer *xt_gui_media_player(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || !isMediaElement(node)) return nullptr;
  auto found = win->media_players.find(node);
  if (found != win->media_players.end()) return found->second.player.get();
  std::string src = sourceOf(node);
  if (src.empty()) return nullptr;
  return openEntry(win, node, src);
}

void xt_gui_media_forget(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr) return;
  auto found = win->media_players.find(node);
  if (found == win->media_players.end()) return;
  releasePlayerTexture(found->second.player.get());
  win->media_players.erase(found);
}

void xt_gui_media_sync(XtGuiWindow *win) {
  if (win == nullptr || win->document == nullptr) return;
  std::vector<const xtgui::Node *> nodes;
  collectMedia(win->document->root(), &nodes);
  std::unordered_set<const xtgui::Node *> live(nodes.begin(), nodes.end());

  /* Drop decoders whose element left the tree. Comparing pointers only (never
   * dereferencing) is safe for a node that was already destroyed. */
  for (auto it = win->media_players.begin(); it != win->media_players.end();) {
    if (live.count(it->first) == 0) {
      releasePlayerTexture(it->second.player.get());
      it = win->media_players.erase(it);
    } else {
      ++it;
    }
  }

  /* Open decoders for new sources and reopen when an element's `src` changes. */
  for (const xtgui::Node *node : nodes) {
    const std::string src = sourceOf(node);
    auto found = win->media_players.find(node);
    if (src.empty()) {
      if (found != win->media_players.end()) {
        releasePlayerTexture(found->second.player.get());
        win->media_players.erase(found);
      }
      continue;
    }
    if (found != win->media_players.end() && found->second.src == src) continue;
    if (found != win->media_players.end()) {
      releasePlayerTexture(found->second.player.get());
      win->media_players.erase(found);
    }
    openEntry(win, node, src);
  }

  /* Publish the intrinsic size of every `<video>` so layout can use it. */
  for (const xtgui::Node *node : nodes) {
    if (!node->isTag("video")) continue;
    auto found = win->media_players.find(node);
    xtgui::MediaPlayer *player = found == win->media_players.end() ? nullptr : found->second.player.get();
    float width = 0.0f;
    float height = 0.0f;
    uint64_t id = 0;
    if (player != nullptr) {
      width = (float)player->frame_width();
      height = (float)player->frame_height();
      id = player->id();
    }
    win->document->setMediaSize(node, width, height, id);
  }
}

void xt_gui_media_tick(XtGuiWindow *win, double now_ms) {
  if (win == nullptr) return;
  /* Snapshot the keys: a handler may change an element's `src`, which erases and
   * reinserts into the table while we iterate. */
  std::vector<const xtgui::Node *> nodes;
  nodes.reserve(win->media_players.size());
  for (const auto &entry : win->media_players) nodes.push_back(entry.first);
  for (const xtgui::Node *node : nodes) {
    auto found = win->media_players.find(node);
    if (found == win->media_players.end() || !found->second.player) continue;
    unsigned events = found->second.player->tick(now_ms);
    /* A freshly decoded frame must reach the image upload. The geometry pass
     * rebuilds the image batches, so mark it dirty once per new generation. */
    if (found->second.player->has_video() &&
        found->second.player->frame_generation() != found->second.rendered_generation) {
      found->second.rendered_generation = found->second.player->frame_generation();
      win->geometry.dirty = 1;
    }
    emitMediaEvents(win, node, events);
  }
}

void xt_gui_media_shutdown(XtGuiWindow *win) {
  if (win == nullptr) return;
  for (auto &entry : win->media_players) releasePlayerTexture(entry.second.player.get());
  win->media_players.clear();
}
