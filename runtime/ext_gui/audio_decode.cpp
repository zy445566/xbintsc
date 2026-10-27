/*
 * xbintsc GUI engine — Web Audio byte decoding.
 *
 * `AudioContext.decodeAudioData` accepts an ArrayBuffer. WAV (PCM, IEEE float,
 * A-law and µ-law) is decoded here with no dependency, which keeps the common
 * "generate a WAV in TypeScript / play a .wav file" case working with `gui`
 * alone. Every other container and codec is delegated to the optional `media`
 * extension, whose FFmpeg build already carries MP3, AAC, Vorbis, Opus and
 * FLAC (see `xt_media_decode_audio` in `media.h`).
 *
 * The output is interleaved, which is what the FFmpeg bridge naturally
 * produces; `audio.cpp` deinterleaves it into the AudioBuffer's channels and
 * the mixer's planar clip.
 */

#include "audio_decode.h"

#include <cmath>
#include <cstring>

#include "media.h"

namespace xtgui {

namespace {

uint16_t read_u16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t read_u32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool fail(std::string *error, const char *message) {
  if (error != nullptr) *error = message;
  return false;
}

/** G.711 A-law, as stored in a WAV `ALAW` stream: after the 0x55 XOR the sign
 * bit (0x80) is set for positive samples. */
double decode_alaw(uint8_t value) {
  value ^= 0x55;
  int sign = value & 0x80 ? 1 : -1;
  int exponent = (value >> 4) & 0x07;
  int mantissa = value & 0x0f;
  int magnitude = exponent == 0 ? (mantissa << 4) + 8
                                : ((mantissa << 4) + 0x108) << (exponent - 1);
  return sign * magnitude / 32768.0;
}

/** G.711 µ-law; the WAV variant is the complement of the raw bit stream. */
double decode_mulaw(uint8_t value) {
  value = static_cast<uint8_t>(~value);
  int sign = value & 0x80 ? -1 : 1;
  int exponent = (value >> 4) & 0x07;
  int mantissa = value & 0x0f;
  int magnitude = (((mantissa << 3) + 0x84) << exponent) - 0x84;
  return sign * magnitude / 32768.0;
}

struct WavFormat {
  uint16_t tag = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t block_align = 0;
  uint16_t bits = 0;
  uint32_t data_offset = 0;
  uint32_t data_size = 0;
};

bool parse_wav(const uint8_t *data, size_t size, WavFormat *format, std::string *error) {
  if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
    return fail(error, "not a WAV file");
  }
  size_t pos = 12;
  bool have_format = false;
  bool have_data = false;
  while (pos + 8 <= size) {
    const uint8_t *header = data + pos;
    uint32_t chunk = read_u32(header + 4);
    size_t body = pos + 8;
    if (body > size) break;
    if (std::memcmp(header, "fmt ", 4) == 0) {
      if (chunk < 16 || body + 16 > size) return fail(error, "truncated WAV format chunk");
      format->tag = read_u16(data + body);
      format->channels = read_u16(data + body + 2);
      format->sample_rate = read_u32(data + body + 4);
      format->block_align = read_u16(data + body + 12);
      format->bits = read_u16(data + body + 14);
      /* WAVE_FORMAT_EXTENSIBLE: the real codec is the first two bytes of the
       * SubFormat GUID, after cbSize/validBits/channelMask. */
      if (format->tag == 0xfffe && chunk >= 40 && body + 26 <= size) {
        format->tag = read_u16(data + body + 24);
      }
      have_format = true;
    } else if (std::memcmp(header, "data", 4) == 0) {
      if (body > size) break;
      format->data_offset = static_cast<uint32_t>(body);
      uint64_t available = size - body;
      format->data_size = static_cast<uint32_t>(chunk < available ? chunk : available);
      have_data = true;
      break;
    }
    pos = body + chunk + (chunk & 1u);
  }
  if (!have_format) return fail(error, "WAV file has no format chunk");
  if (!have_data) return fail(error, "WAV file has no data chunk");
  if (format->channels == 0 || format->channels > 32) return fail(error, "invalid WAV channel count");
  if (format->sample_rate < 8000 || format->sample_rate > 192000) {
    return fail(error, "invalid WAV sample rate");
  }
  bool supported = false;
  switch (format->tag) {
    case 1: /* PCM */
      supported = format->bits == 8 || format->bits == 16 || format->bits == 24 ||
                  format->bits == 32;
      break;
    case 3: /* IEEE float */
      supported = format->bits == 32 || format->bits == 64;
      break;
    case 6: /* A-law */
    case 7: /* µ-law */
      supported = format->bits == 8;
      break;
    default:
      break;
  }
  if (!supported) return fail(error, "unsupported WAV codec");
  return true;
}

double pcm_sample(const uint8_t *data, uint16_t bits) {
  switch (bits) {
    case 8:
      return (static_cast<int>(data[0]) - 128) / 128.0;
    case 16:
      return static_cast<int16_t>(data[0] | (data[1] << 8)) / 32768.0;
    case 24: {
      int32_t value = data[0] | (data[1] << 8) | (data[2] << 16);
      if (value & 0x800000) value |= ~0xffffff;
      return value / 8388608.0;
    }
    case 32: {
      int32_t value = static_cast<int32_t>(read_u32(data));
      return value / 2147483648.0;
    }
    default:
      return 0.0;
  }
}

bool decode_wav(const uint8_t *data, size_t size, DecodedAudio *out, std::string *error) {
  WavFormat format;
  if (!parse_wav(data, size, &format, error)) return false;
  uint32_t frame_bytes;
  switch (format.tag) {
    case 1:
      frame_bytes = format.channels * (format.bits / 8u);
      break;
    case 3:
      frame_bytes = format.channels * (format.bits / 8u);
      break;
    default:
      frame_bytes = format.channels;
      break;
  }
  if (format.block_align > frame_bytes) frame_bytes = format.block_align;
  if (frame_bytes == 0) return fail(error, "invalid WAV frame size");
  uint32_t frames = format.data_size / frame_bytes;
  if (frames == 0) return fail(error, "WAV file has no samples");

  out->sample_rate = static_cast<int>(format.sample_rate);
  out->channels = format.channels;
  out->frames = frames;
  out->interleaved.assign(static_cast<size_t>(frames) * format.channels, 0.0f);
  for (uint32_t frame = 0; frame < frames; frame++) {
    const uint8_t *source = data + format.data_offset + static_cast<size_t>(frame) * frame_bytes;
    for (uint16_t channel = 0; channel < format.channels; channel++) {
      const uint8_t *sample = source + channel * (format.bits / 8u);
      double value;
      if (format.tag == 3) {
        if (format.bits == 32) {
          float narrowed;
          std::memcpy(&narrowed, sample, sizeof(narrowed));
          value = narrowed;
        } else {
          double wide;
          std::memcpy(&wide, sample, sizeof(wide));
          value = wide;
        }
      } else if (format.tag == 6) {
        value = decode_alaw(sample[0]);
      } else if (format.tag == 7) {
        value = decode_mulaw(sample[0]);
      } else {
        value = pcm_sample(sample, format.bits);
      }
      out->interleaved[static_cast<size_t>(frame) * format.channels + channel] =
          static_cast<float>(value);
    }
  }
  return true;
}

}  // namespace

bool decode_audio_bytes(const uint8_t *data, size_t size, DecodedAudio *out, std::string *error) {
  if (data == nullptr || size == 0) return fail(error, "empty audio data");
  std::string wav_error;
  if (decode_wav(data, size, out, &wav_error)) return true;
  std::string media_error;
  if (xt_media_decode_audio(data, size, out, &media_error)) return true;
  if (error != nullptr) *error = media_error.empty() ? wav_error : media_error;
  return false;
}

}  // namespace xtgui
