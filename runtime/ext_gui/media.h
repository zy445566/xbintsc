#ifndef XT_GUI_MEDIA_H
#define XT_GUI_MEDIA_H

/*
 * Media decoding for `<video>` and `<audio>`.
 *
 * The engine owns the DOM integration (one `MediaPlayer` per element, owned by
 * the window) and the renderer uploads decoded frames as GPU textures. The
 * decoder itself is optional: `MediaPlayer::open` dispatches through the table
 * the `media` extension installs at startup (see `media_api.h`). A program built
 * with `--ext gui` alone therefore links without FFmpeg and reports an `error`
 * event when a media element is opened.
 *
 * A `MediaPlayer` owns one demux/decode pipeline for a single source file. It
 * turns the file into two streams of interest:
 *
 *   - the latest decoded video frame (RGBA8, `Image`), which the renderer
 *     uploads as a GPU texture and stretches over the element's box;
 *   - 48 kHz stereo float PCM, which is written to the caller-provided
 *     `AudioSink`.
 *
 * Decoding happens on the main thread (see the threading note in the video
 * extension's `media.cpp`): `tick()` advances the wall clock, decodes whatever
 * is due and returns a bitmask of DOM events the caller should dispatch. The
 * engine already runs a frame loop, so there is no separate media thread and no
 * interaction with the garbage collector.
 *
 * The FFmpeg headers stay inside the media extension's `media.cpp`; this header
 * only exposes value types so the rest of the engine (and `gui_engine.h`) can
 * hold a player without pulling in libav*.
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "image.h"
#include "media_api.h"

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
 * The audio output the decoder writes PCM to.
 *
 * The engine implements it with an SDL audio stream (`gui_media.cpp`), which
 * keeps SDL out of the optional `media.a`/`media.lib`. The fixed format is 48 kHz stereo
 * float: the decoder resamples every source stream to it with swresample.
 */
class AudioSink {
 public:
  virtual ~AudioSink() {}

  /** Open the output device. Returns false when unavailable (headless CI);
   * playback then continues silently and `write` becomes a no-op. */
  virtual bool open() = 0;
  virtual void write(const void *data, size_t bytes) = 0;
  virtual void clear() = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void set_gain(float gain) = 0;
  /** Seconds of PCM queued ahead of the play head. */
  virtual double queued_seconds() const = 0;
};

/**
 * One decoder for one `<video>`/`<audio>` source.
 *
 * Instances are created with `open()`, which returns `nullptr` and fills
 * `*error` when the file cannot be read or when the enabled extensions do not
 * include a decoder (the `media` extension). All times are seconds.
 */
class MediaPlayer {
 public:
  virtual ~MediaPlayer() {}

  /** Open `path`, taking ownership of `audio`. Returns `nullptr` (with `*error`
   * set) on failure; the audio sink is released in that case too. */
  static std::unique_ptr<MediaPlayer> open(const std::string &path, std::string *error,
                                           std::unique_ptr<AudioSink> audio);

  /** Process-wide identity used to key GPU textures and the lookup below. */
  virtual uint64_t id() const = 0;
  virtual const std::string &source() const = 0;
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
  virtual const Image *frame() const = 0;
  /** Bumped every time `frame()` changes, so the renderer can re-upload. */
  virtual uint64_t frame_generation() const = 0;

  /** Advance the clock, decode/buffer, and return the events that happened. */
  virtual unsigned tick(double now_ms) = 0;

  /** Drop the cached GPU texture handle (called when it is released). */
  virtual void invalidate_texture() = 0;
};

/** The live player for `id`, or `nullptr`. Used by the renderer. */
MediaPlayer *xt_media_by_id(uint64_t id);

/** Tear down process-wide media state (id table, decoder state). */
void xt_media_shutdown();

}  // namespace xtgui

#endif  // XT_GUI_MEDIA_H
