/*
 * xbintsc GUI engine — the AudioBuffer bridge.
 *
 * An AudioBuffer is a JavaScript object whose channels are real
 * `Float32Array`s, so a page can read and write samples with the normal typed
 * array API. These helpers convert between that representation and the native
 * `AudioClip` the mixer reads, and between an ArrayBuffer/typed-array argument
 * and the bytes a decoder consumes.
 */
#ifndef XT_GUI_AUDIO_BUFFER_H
#define XT_GUI_AUDIO_BUFFER_H

#include <cstdint>
#include <memory>
#include <vector>

#include "rt.h"

namespace xtgui {

struct AudioClip;

/** Build an AudioBuffer value from planar float channels. The channel count and
 * length come from the vectors; `sample_rate` is the buffer's own rate. */
xt_value audio_buffer_create(const std::vector<std::vector<float>> &channels, int sample_rate);

/** Copy an AudioBuffer value's channels into a mixer clip. Returns null when the
 * value is not an AudioBuffer or has no samples. */
std::shared_ptr<AudioClip> audio_clip_from_buffer(xt_value buffer);

/** Read the bytes behind an ArrayBuffer or a typed array view (for
 * `decodeAudioData`). */
bool audio_bytes_from_value(xt_value value, std::vector<uint8_t> *out);

}  // namespace xtgui

#endif /* XT_GUI_AUDIO_BUFFER_H */
