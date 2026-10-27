/*
 * xbintsc GUI engine — Web Audio byte decoding.
 *
 * `decodeAudioBytes` is what `AudioContext.decodeAudioData` calls. WAV is
 * decoded by the engine itself; every other container/codec is delegated to
 * the optional `media` extension, so `gui` alone still has a working decoder
 * for the format a page usually generates.
 */
#ifndef XT_GUI_AUDIO_DECODE_H
#define XT_GUI_AUDIO_DECODE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xtgui {

/** Decoded PCM, interleaved, `frames * channels` floats. */
struct DecodedAudio {
  int channels = 0;
  int sample_rate = 0;
  uint32_t frames = 0;
  std::vector<float> interleaved;
};

/**
 * Decode `size` bytes of `data`.
 *
 * WAV is handled in-engine (PCM 8/16/24/32, IEEE float 32/64, A-law, µ-law).
 * Anything else asks the `media` extension's FFmpeg decoder; without that
 * extension the call fails with an actionable message. Returns false and fills
 * `*error` on failure. `data` is not retained.
 */
bool decode_audio_bytes(const uint8_t *data, size_t size, DecodedAudio *out, std::string *error);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_DECODE_H */
