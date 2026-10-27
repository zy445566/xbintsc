/*
 * xbintsc media extension — in-memory audio decoding.
 *
 * `AudioContext.decodeAudioData` hands over bytes, not a path, so the file
 * oriented `MediaPlayer` cannot be reused. This unit opens the bytes through a
 * custom `AVIOContext`, decodes the best audio stream to interleaved float PCM
 * at the stream's own sample rate, and leaves resampling to the mixer (which
 * already resamples an AudioBuffer whose rate differs from 48 kHz).
 *
 * The ABI is C (`media_api.h`): `gui.a` stays FFmpeg-free and reaches this
 * object through the table the media bridge installs. Both archives are built
 * against the same static CRT, so the caller may `free()` the buffer through
 * `xt_media_backend_free_audio`.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include "../ext_gui/media_api.h"

namespace {

struct MemoryReader {
  const unsigned char *data;
  size_t size;
  size_t position;
};

int read_packet(void *opaque, uint8_t *buffer, int size) {
  auto *reader = static_cast<MemoryReader *>(opaque);
  if (reader->position >= reader->size) return AVERROR_EOF;
  size_t available = reader->size - reader->position;
  int take = size < static_cast<int>(available) ? size : static_cast<int>(available);
  if (take <= 0) return AVERROR_EOF;
  std::memcpy(buffer, reader->data + reader->position, static_cast<size_t>(take));
  reader->position += static_cast<size_t>(take);
  return take;
}

int64_t seek_stream(void *opaque, int64_t offset, int whence) {
  auto *reader = static_cast<MemoryReader *>(opaque);
  if (whence == AVSEEK_SIZE) return static_cast<int64_t>(reader->size);
  int64_t base;
  if (whence == SEEK_SET) {
    base = 0;
  } else if (whence == SEEK_CUR) {
    base = static_cast<int64_t>(reader->position);
  } else if (whence == SEEK_END) {
    base = static_cast<int64_t>(reader->size);
  } else {
    return AVERROR(EINVAL);
  }
  int64_t target = base + offset;
  if (target < 0 || target > static_cast<int64_t>(reader->size)) return AVERROR(EINVAL);
  reader->position = static_cast<size_t>(target);
  return target;
}

void set_error(char *error, size_t size, const char *message) {
  if (error != nullptr && size > 0) std::snprintf(error, size, "%s", message);
}

void set_ffmpeg_error(char *error, size_t size, const char *prefix, int code) {
  char detail[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(code, detail, sizeof(detail));
  if (error != nullptr && size > 0) {
    std::snprintf(error, size, "%s: %s", prefix, detail);
  }
}

/** The decoder pipeline for one in-memory file. Destructor releases everything,
 * which keeps the early-return paths in `decode` readable. */
struct Decoder {
  MemoryReader reader{nullptr, 0, 0};
  AVIOContext *io = nullptr;
  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *frame = nullptr;
  SwrContext *resampler = nullptr;
  AVChannelLayout out_layout{};
  int out_channels = 0;
  int sample_rate = 0;
  int stream_index = -1;
  std::vector<float> samples;
  bool failed = false;
};

void close_decoder(Decoder *decoder) {
  swr_free(&decoder->resampler);
  av_channel_layout_uninit(&decoder->out_layout);
  if (decoder->frame != nullptr) av_frame_free(&decoder->frame);
  if (decoder->packet != nullptr) av_packet_free(&decoder->packet);
  if (decoder->codec != nullptr) avcodec_free_context(&decoder->codec);
  if (decoder->format != nullptr) avformat_close_input(&decoder->format);
  if (decoder->io != nullptr) {
    av_freep(&decoder->io->buffer);
    avio_context_free(&decoder->io);
  }
}

/** Lazily configure the resampler from the first decoded frame: some files only
 * reveal their channel layout once decoding starts. */
bool prepare_resampler(Decoder *decoder, const AVFrame *frame, char *error, size_t error_size) {
  if (decoder->resampler != nullptr) return true;
  AVChannelLayout in_layout = frame->ch_layout;
  if (in_layout.nb_channels <= 0) in_layout = decoder->codec->ch_layout;
  int channels = in_layout.nb_channels > 0 ? in_layout.nb_channels : 1;
  int rate = frame->sample_rate > 0 ? frame->sample_rate : decoder->codec->sample_rate;
  if (rate <= 0) {
    set_error(error, error_size, "audio stream has no usable sample rate");
    return false;
  }
  decoder->out_channels = channels > 2 ? 2 : channels;
  decoder->sample_rate = rate;
  av_channel_layout_default(&decoder->out_layout, decoder->out_channels);
  int result = swr_alloc_set_opts2(&decoder->resampler, &decoder->out_layout, AV_SAMPLE_FMT_FLT,
                                   decoder->sample_rate, &in_layout,
                                   static_cast<AVSampleFormat>(frame->format), rate, 0, nullptr);
  if (result < 0 || decoder->resampler == nullptr ||
      swr_init(decoder->resampler) < 0) {
    set_error(error, error_size, "cannot set up audio resampling");
    return false;
  }
  return true;
}

/** Convert one decoded frame and append it to `decoder->samples`. */
bool append_frame(Decoder *decoder, const AVFrame *frame, char *error, size_t error_size) {
  if (!prepare_resampler(decoder, frame, error, error_size)) return false;
  int64_t delay = swr_get_delay(decoder->resampler, frame->sample_rate);
  int capacity = static_cast<int>(av_rescale_rnd(delay + frame->nb_samples, decoder->sample_rate,
                                                 frame->sample_rate, AV_ROUND_UP));
  if (capacity <= 0) return true;
  std::vector<float> converted(static_cast<size_t>(capacity) * decoder->out_channels);
  uint8_t *out_data[1] = {reinterpret_cast<uint8_t *>(converted.data())};
  int got = swr_convert(decoder->resampler, out_data, capacity,
                        const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
  if (got < 0) {
    set_ffmpeg_error(error, error_size, "cannot resample audio", got);
    return false;
  }
  size_t count = static_cast<size_t>(got) * decoder->out_channels;
  decoder->samples.insert(decoder->samples.end(), converted.begin(),
                          converted.begin() + static_cast<std::ptrdiff_t>(count));
  return true;
}

/** Drain the decoder after the last packet. */
bool flush_decoder(Decoder *decoder, char *error, size_t error_size) {
  int result = avcodec_send_packet(decoder->codec, nullptr);
  if (result < 0 && result != AVERROR_EOF) {
    set_ffmpeg_error(error, error_size, "cannot flush audio decoder", result);
    return false;
  }
  while (true) {
    result = avcodec_receive_frame(decoder->codec, decoder->frame);
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
    if (result < 0) {
      set_ffmpeg_error(error, error_size, "cannot read audio frame", result);
      return false;
    }
    if (!append_frame(decoder, decoder->frame, error, error_size)) return false;
    av_frame_unref(decoder->frame);
  }
}

bool decode_packets(Decoder *decoder, char *error, size_t error_size) {
  while (true) {
    int result = av_read_frame(decoder->format, decoder->packet);
    if (result == AVERROR_EOF) break;
    if (result < 0) {
      set_ffmpeg_error(error, error_size, "cannot read audio packet", result);
      return false;
    }
    if (decoder->packet->stream_index != decoder->stream_index) {
      av_packet_unref(decoder->packet);
      continue;
    }
    int sent = avcodec_send_packet(decoder->codec, decoder->packet);
    av_packet_unref(decoder->packet);
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
      set_ffmpeg_error(error, error_size, "cannot decode audio packet", sent);
      return false;
    }
    while (true) {
      result = avcodec_receive_frame(decoder->codec, decoder->frame);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
      if (result < 0) {
        set_ffmpeg_error(error, error_size, "cannot decode audio frame", result);
        return false;
      }
      if (!append_frame(decoder, decoder->frame, error, error_size)) return false;
      av_frame_unref(decoder->frame);
    }
  }
  return flush_decoder(decoder, error, error_size);
}

bool open_decoder(Decoder *decoder, int *stream_index, char *error, size_t error_size) {
  decoder->io = avio_alloc_context(
      static_cast<unsigned char *>(av_malloc(32768)), 32768, 0, &decoder->reader, read_packet,
      nullptr, seek_stream);
  if (decoder->io == nullptr) {
    set_error(error, error_size, "cannot allocate the audio reader");
    return false;
  }
  decoder->format = avformat_alloc_context();
  if (decoder->format == nullptr) {
    set_error(error, error_size, "cannot allocate the audio container");
    return false;
  }
  decoder->format->pb = decoder->io;
  decoder->format->flags |= AVFMT_FLAG_CUSTOM_IO;
  int result = avformat_open_input(&decoder->format, nullptr, nullptr, nullptr);
  if (result < 0) {
    set_ffmpeg_error(error, error_size, "cannot open audio data", result);
    return false;
  }
  result = avformat_find_stream_info(decoder->format, nullptr);
  if (result < 0) {
    set_ffmpeg_error(error, error_size, "cannot read audio metadata", result);
    return false;
  }
  *stream_index = av_find_best_stream(decoder->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (*stream_index < 0) {
    set_error(error, error_size, "audio data has no audio stream");
    return false;
  }
  decoder->stream_index = *stream_index;
  AVCodecParameters *parameters = decoder->format->streams[*stream_index]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
  if (codec == nullptr) {
    set_error(error, error_size, "no decoder for this audio codec");
    return false;
  }
  decoder->codec = avcodec_alloc_context3(codec);
  if (decoder->codec == nullptr) {
    set_error(error, error_size, "cannot allocate the audio decoder");
    return false;
  }
  result = avcodec_parameters_to_context(decoder->codec, parameters);
  if (result < 0) {
    set_ffmpeg_error(error, error_size, "cannot configure the audio decoder", result);
    return false;
  }
  result = avcodec_open2(decoder->codec, codec, nullptr);
  if (result < 0) {
    set_ffmpeg_error(error, error_size, "cannot start the audio decoder", result);
    return false;
  }
  decoder->packet = av_packet_alloc();
  decoder->frame = av_frame_alloc();
  if (decoder->packet == nullptr || decoder->frame == nullptr) {
    set_error(error, error_size, "cannot allocate decoder buffers");
    return false;
  }
  return true;
}

}  // namespace

extern "C" int xt_media_backend_decode_audio(const unsigned char *data, size_t size,
                                             XtMediaAudioBuffer *out, char *error,
                                             size_t error_size) {
  if (data == nullptr || size == 0 || out == nullptr) {
    set_error(error, error_size, "empty audio data");
    return 0;
  }
  std::memset(out, 0, sizeof(*out));
  Decoder decoder;
  decoder.reader = {data, size, 0};
  int stream_index = -1;
  bool ok = open_decoder(&decoder, &stream_index, error, error_size) &&
            decode_packets(&decoder, error, error_size);
  if (!ok || decoder.out_channels <= 0 || decoder.samples.empty()) {
    if (ok) set_error(error, error_size, "audio data has no samples");
    close_decoder(&decoder);
    return 0;
  }
  int frames = static_cast<int>(decoder.samples.size() / decoder.out_channels);
  size_t bytes = decoder.samples.size() * sizeof(float);
  auto *samples = static_cast<float *>(std::malloc(bytes));
  if (samples == nullptr) {
    set_error(error, error_size, "cannot allocate the decoded audio buffer");
    close_decoder(&decoder);
    return 0;
  }
  std::memcpy(samples, decoder.samples.data(), bytes);
  out->channels = decoder.out_channels;
  out->sample_rate = decoder.sample_rate;
  out->frames = frames;
  out->samples = samples;
  close_decoder(&decoder);
  return 1;
}

extern "C" void xt_media_backend_free_audio(void *samples) { std::free(samples); }
