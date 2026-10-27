/*
 * xbintsc GUI engine — AudioBuffer value construction and sample transfer.
 *
 * The mixer never sees an `xt_value`: `audio_clip_from_buffer` copies the
 * samples a BufferSource needs when it starts, so a later collection of the
 * AudioBuffer (or a mutation of its Float32Arrays) cannot race the audio
 * thread. `audio_buffer_create` fills a freshly decoded buffer through the
 * runtime's bulk ArrayBuffer copy instead of one JS property write per sample.
 */

#include "audio_buffer.h"

#include <cstring>

#include "audio_mixer.h"

namespace xtgui {

namespace {

/* Byte endianness matches the host, which is what the runtime's typed array
 * read/write also assumes (it memcpy's the element bytes). */
xt_value make_channel(const float *samples, uint32_t length) {
  xt_value array_buffer = xt_array_buffer_alloc(length * static_cast<uint32_t>(sizeof(float)));
  if (samples != nullptr && length > 0) {
    xt_array_buffer_copy_bytes_in(array_buffer, 0,
                                  reinterpret_cast<const uint8_t *>(samples),
                                  length * static_cast<uint32_t>(sizeof(float)));
  }
  xt_value constructor_arg = array_buffer;
  return xt_typed_array_ctor_by_name(xt_string_from_cstr("Float32Array"), 1, &constructor_arg);
}

xt_value buffer_get_channel_data(xt_value self, xt_value env, int32_t argc, xt_value *argv);

xt_value empty_channel() {
  xt_value zero = xt_number(0);
  return xt_typed_array_ctor_by_name(xt_string_from_cstr("Float32Array"), 1, &zero);
}

/** A channel's bytes: the typed array's backing buffer plus its view range. */
bool channel_bytes(xt_value channel, std::vector<uint8_t> *out, uint32_t *length) {
  if (!xt_is_typed_array(channel)) return false;
  xt_value buffer = xt_typed_array_backing_buffer(channel);
  uint32_t offset = xt_typed_array_view_offset(channel);
  uint32_t byte_length =
      static_cast<uint32_t>(xt_to_number(xt_object_get_cstr(channel, "byteLength")));
  if (!XT_IS_OBJECT(buffer) || byte_length < sizeof(float)) return false;
  out->resize(byte_length);
  if (xt_array_buffer_copy_bytes_out(buffer, offset, out->data(), byte_length) != byte_length) {
    return false;
  }
  *length = byte_length / static_cast<uint32_t>(sizeof(float));
  return true;
}

}  // namespace

xt_value audio_buffer_create(const std::vector<std::vector<float>> &channels, int sample_rate) {
  uint32_t length = channels.empty() ? 0 : static_cast<uint32_t>(channels[0].size());
  xt_value channel_array = xt_array_new(0, nullptr);
  for (const std::vector<float> &channel : channels) {
    xt_array_push(channel_array, make_channel(channel.data(),
                                              static_cast<uint32_t>(channel.size())));
  }
  xt_value buffer = xt_object_new();
  xt_object_set(buffer, xt_string_from_cstr("__xt_audio_channels"), channel_array);
  xt_object_set(buffer, xt_string_from_cstr("getChannelData"),
                xt_closure_new(reinterpret_cast<void *>(buffer_get_channel_data), 0, nullptr));
  xt_object_set(buffer, xt_string_from_cstr("numberOfChannels"),
                xt_number(static_cast<double>(channels.size())));
  xt_object_set(buffer, xt_string_from_cstr("length"), xt_number(static_cast<double>(length)));
  xt_object_set(buffer, xt_string_from_cstr("sampleRate"),
                xt_number(static_cast<double>(sample_rate)));
  xt_object_set(buffer, xt_string_from_cstr("duration"),
                xt_number(sample_rate > 0 ? static_cast<double>(length) / sample_rate : 0.0));
  xt_object_set(buffer, xt_string_from_cstr("__xt_audio_ready"), XT_TRUE);
  return buffer;
}

std::shared_ptr<AudioClip> audio_clip_from_buffer(xt_value buffer) {
  xt_value channels = xt_object_get_cstr(buffer, "__xt_audio_channels");
  if (!XT_IS_ARRAY(channels) && !XT_IS_OBJECT(channels)) return nullptr;
  int count = xt_array_size(channels);
  if (count <= 0) return nullptr;
  if (count > 2) count = 2; /* the mixer is stereo; extra channels are dropped */
  auto clip = std::make_shared<AudioClip>();
  clip->sample_rate =
      static_cast<int>(xt_to_number(xt_object_get_cstr(buffer, "sampleRate")));
  if (clip->sample_rate <= 0) clip->sample_rate = kAudioMixRate;
  for (int index = 0; index < count; index++) {
    xt_value channel = xt_array_get(channels, xt_number(static_cast<double>(index)));
    std::vector<uint8_t> bytes;
    uint32_t length = 0;
    if (!channel_bytes(channel, &bytes, &length)) return nullptr;
    std::vector<float> samples(length);
    if (length > 0) std::memcpy(samples.data(), bytes.data(), bytes.size());
    if (index == 0) {
      clip->length = length;
    } else if (length != clip->length) {
      /* Keep every channel the same length; the mixer indexes all of them with
       * the first channel's length. */
      if (length < clip->length) clip->length = length;
      for (std::vector<float> &existing : clip->channels) existing.resize(clip->length);
      samples.resize(clip->length);
    }
    clip->channels.push_back(std::move(samples));
  }
  if (clip->length == 0) return nullptr;
  return clip;
}

bool audio_bytes_from_value(xt_value value, std::vector<uint8_t> *out) {
  /* `fs.readFileSync` returns a byte string in this runtime (there is no
   * dedicated binary value type). Its storage is the file's raw bytes, so a
   * decoded audio file can be handed over directly. */
  if (XT_IS_STRING(value)) {
    int32_t bytes = xt_string_length_value(value);
    const char *data = xt_string_data(value);
    if (data == nullptr || bytes <= 0) return false;
    out->assign(data, data + bytes);
    return true;
  }
  xt_value buffer = value;
  uint32_t offset = 0;
  uint32_t length = 0;
  if (xt_is_typed_array(value)) {
    /* A typed array view: decode only the view's bytes. */
    buffer = xt_typed_array_backing_buffer(value);
    offset = xt_typed_array_view_offset(value);
    length = static_cast<uint32_t>(xt_to_number(xt_object_get_cstr(value, "byteLength")));
  } else {
    length = static_cast<uint32_t>(xt_to_number(xt_object_get_cstr(value, "byteLength")));
  }
  if (XT_IS_OBJECT(buffer) && length > 0) {
    out->resize(length);
    return xt_array_buffer_copy_bytes_out(buffer, offset, out->data(), length) == length;
  }
  /* A Node `Buffer` is a plain object with numeric elements and a `length`
   * (xbintsc has no dedicated binary value type). Node programs routinely pass
   * `readFileSync(...)` straight to `decodeAudioData`, so accept that shape. */
  xt_value count = xt_object_get_cstr(value, "length");
  if (!XT_IS_OBJECT(value) || !XT_IS_NUMBER(count) || xt_to_number(count) <= 0) return false;
  uint32_t bytes = static_cast<uint32_t>(xt_to_number(count));
  out->resize(bytes);
  for (uint32_t index = 0; index < bytes; index++) {
    (*out)[index] = static_cast<uint8_t>(
        xt_to_number(xt_object_get(value, xt_number(static_cast<double>(index)))));
  }
  return true;
}

namespace {

xt_value buffer_get_channel_data(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value channels = xt_object_get_cstr(self, "__xt_audio_channels");
  int index = static_cast<int>(xt_to_number(xt_arg(argc, argv, 0)));
  if (!XT_IS_ARRAY(channels) && !XT_IS_OBJECT(channels)) return empty_channel();
  if (index < 0 || index >= xt_array_size(channels)) return empty_channel();
  return xt_array_get(channels, xt_number(static_cast<double>(index)));
}

}  // namespace

}  // namespace xtgui
