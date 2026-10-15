#ifndef XT_GUI_MEDIA_H
#define XT_GUI_MEDIA_H

/*
 * Media decoding for `<video>` and `<audio>`.
 *
 * A `MediaPlayer` owns one FFmpeg demux/decode pipeline for a single source
 * file. It turns the file into two streams of interest:
 *
 *   - the latest decoded video frame (RGBA8, `Image`), which the renderer
 *     uploads as a GPU texture and stretches over the element's box;
 *   - 48 kHz stereo float PCM, which is pushed into an SDL audio stream.
 *
 * Decoding happens on the main thread (see the threading note in `media.cpp`):
 * `tick()` advances the wall clock, decodes whatever is due and returns a
 * bitmask of DOM events the caller should dispatch. The engine already runs a
 * frame loop, so there is no separate media thread and no interaction with the
 * garbage collector.
 *
 * The FFmpeg headers stay inside `media.cpp`; this header only exposes value
 * types so the rest of the engine (and `gui_engine.h`) can hold a player
 * without pulling in libav*.
 */

#include <cstdint>
#include <memory>
#include <string>

#include "image.h"

namespace xtgui {

/** DOM events a media element may fire after `tick()`. */
enum MediaEvent {
  kMediaEventLoadedMetadata = 1 << 0,
  kMediaEventPlay = 1 << 1,
  kMediaEventPause = 1 << 2,
  kMediaEventEnded = 1 << 3,
  kMediaEventTimeUpdate = 1 << 4,
  kMediaEventError = 1 << 5,
};

/**
 * One decoder for one `<video>`/`<audio>` source.
 *
 * Instances are created with `open()`, which returns `nullptr` and fills
 * `*error` when the file cannot be read or when the build has no decoder
 * (`gui.a` built without FFmpeg). All times are seconds.
 */
class MediaPlayer {
 public:
  virtual ~MediaPlayer() {}

  /** Open `path`; returns `nullptr` (with `*error` set) on failure. */
  static std::unique_ptr<MediaPlayer> open(const std::string& path, std::string* error);

  /** Process-wide identity used to key GPU textures and the lookup below. */
  virtual uint64_t id() const = 0;
  virtual const std::string& source() const = 0;
  virtual bool has_video() const = 0;
  virtual bool has_audio() const = 0;
  virtual double duration() const = 0;

  /* Playback state ------------------------------------------------------- */
  virtual bool paused() const = 0;
  virtual bool ended() const = 0;
  virtual double current_time() const = 0;
  virtual double volume() const = 0;
  virtual bool muted() const = 0;
  virtual bool loop() const = 0;
  virtual bool autoplay() const = 0;

  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void seek(double seconds) = 0;
  virtual void set_volume(double value) = 0;
  virtual void set_muted(bool value) = 0;
  virtual void set_loop(bool value) = 0;
  virtual void set_autoplay(bool value) = 0;

  /* Video ---------------------------------------------------------------- */
  virtual int frame_width() const = 0;
  virtual int frame_height() const = 0;
  /** Latest decoded RGBA frame, or `nullptr` before the first one is ready. */
  virtual const Image* frame() const = 0;
  /** Bumped every time `frame()` changes, so the renderer can re-upload. */
  virtual uint64_t frame_generation() const = 0;

  /** Advance the clock, decode/buffer, and return the events that happened. */
  virtual unsigned tick(double now_ms) = 0;

  /** Drop the cached GPU texture handle (called when it is released). */
  virtual void invalidate_texture() = 0;
};

/** The live player for `id`, or `nullptr`. Used by the renderer. */
MediaPlayer* xt_media_by_id(uint64_t id);

/** Tear down process-wide media state (audio subsystem, id table). */
void xt_media_shutdown();

}  // namespace xtgui

#endif  // XT_GUI_MEDIA_H
