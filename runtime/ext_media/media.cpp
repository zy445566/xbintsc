/*
 * xbintsc media extension — `<video>`/`<audio>` decoding.
 *
 * This translation unit is the only place that knows about FFmpeg. It is
 * compiled into the prebuilt `media.a`/`media.lib` by `scripts/build-media.ts` and linked
 * only when the `media` extension is enabled; the GUI engine itself is
 * decoder-free (see `../ext_gui/media_api.h`).
 *
 * Threading: the engine is single-threaded and its values live on the main
 * thread, so nothing here may run on an audio callback. Instead the decoder is
 * pushed from `tick()` on the main thread: it writes PCM into the
 * `xtgui::AudioSink` supplied by the GUI (an SDL audio stream opened in push
 * mode), and SDL pulls from its queued buffer on its own thread.
 * `queued_seconds()` keeps the buffer shallow so there is no unbounded latency.
 */

#include "../ext_gui/media.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace xtgui {

namespace {

std::unordered_map<uint64_t, MediaPlayer*>& PlayerTable() {
  static std::unordered_map<uint64_t, MediaPlayer*> table;
  return table;
}

/** Audio format everything is resampled to before it reaches the sink. */
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
/** How much audio to keep buffered ahead of the play head. */
constexpr double kAudioAhead = 0.25;
/** Minimum gap between `timeupdate` events. */
constexpr double kTimeUpdateIntervalMs = 250.0;

/** Monotonic wall clock, the reference for playback timing. */
double NowMs() {
  static const auto start = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

uint64_t NextId() {
  static uint64_t next = 1;
  return next++;
}

struct QueuedFrame {
  Image image;
  double pts = 0;
};

class FfmpegPlayer final : public MediaPlayer {
 public:
  explicit FfmpegPlayer(std::unique_ptr<AudioSink> audio) : audio_(std::move(audio)) {}

  ~FfmpegPlayer() override {
    PlayerTable().erase(id_);
    if (sws_) sws_freeContext(sws_);
    if (swr_) swr_free(&swr_);
    if (vframe_) av_frame_free(&vframe_);
    if (aframe_) av_frame_free(&aframe_);
    if (pkt_) av_packet_free(&pkt_);
    if (vctx_) avcodec_free_context(&vctx_);
    if (actx_) avcodec_free_context(&actx_);
    if (fmt_) avformat_close_input(&fmt_);
  }

  bool Open(const std::string& path, std::string* error) {
    source_ = path;
    if (avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr) < 0) {
      if (error) *error = "cannot open media: " + path;
      return false;
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
      if (error) *error = "cannot read media metadata: " + path;
      return false;
    }
    duration_ = fmt_->duration > 0 ? static_cast<double>(fmt_->duration) / AV_TIME_BASE : 0.0;

    vstream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    astream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (vstream_ < 0 && astream_ < 0) {
      if (error) *error = "no audio or video stream: " + path;
      return false;
    }
    if (vstream_ >= 0 && !OpenVideo(fmt_->streams[vstream_])) {
      vstream_ = -1;
    }
    if (astream_ >= 0 && !OpenAudio(fmt_->streams[astream_])) {
      astream_ = -1;
    }
    if (vstream_ < 0 && astream_ < 0) {
      if (error) *error = "no supported stream: " + path;
      return false;
    }
    pkt_ = av_packet_alloc();
    vframe_ = av_frame_alloc();
    aframe_ = av_frame_alloc();

    /* A file whose default duration is unknown falls back to the last
     * timestamp of the video stream, which is enough for the `duration`
     * property and for end-of-stream detection. */
    if (duration_ <= 0 && vstream_ >= 0) {
      AVStream* vs = fmt_->streams[vstream_];
      if (vs->duration > 0) duration_ = vs->duration * av_q2d(vs->time_base);
    }
    id_ = NextId();
    PlayerTable()[id_] = this;
    pending_events_ |= kMediaEventLoadedMetadata;
    return true;
  }

  uint64_t id() const override { return id_; }
  const std::string& source() const override { return source_; }
  bool has_video() const override { return vstream_ >= 0; }
  bool has_audio() const override { return astream_ >= 0; }
  double duration() const override { return duration_; }

  bool paused() const override { return paused_; }
  bool ended() const override { return ended_; }
  double current_time() const override {
    if (paused_) return clock_base_pts_;
    return clock_base_pts_ + (NowMs() - clock_base_ms_) / 1000.0;
  }
  double volume() const override { return volume_; }
  bool muted() const override { return muted_; }
  bool loop() const override { return loop_; }
  bool autoplay() const override { return autoplay_; }

  void play() override {
    if (ended_) Seek(0);
    if (!paused_) return;
    paused_ = false;
    clock_base_ms_ = NowMs();
    if (audio_ready_) audio_->resume();
    pending_events_ |= kMediaEventPlay;
  }

  void pause() override {
    if (paused_) return;
    clock_base_pts_ = current_time();
    paused_ = true;
    if (audio_ready_) audio_->pause();
    pending_events_ |= kMediaEventPause;
  }

  void seek(double seconds) override { Seek(seconds); }

  void set_volume(double value) override {
    volume_ = std::min(1.0, std::max(0.0, value));
    ApplyGain();
  }
  void set_muted(bool value) override {
    muted_ = value;
    ApplyGain();
  }
  void set_loop(bool value) override { loop_ = value; }
  void set_autoplay(bool value) override { autoplay_ = value; }

  int frame_width() const override { return vctx_ ? vctx_->width : 0; }
  int frame_height() const override { return vctx_ ? vctx_->height : 0; }
  const Image* frame() const override { return current_valid_ ? &current_ : nullptr; }
  uint64_t frame_generation() const override { return generation_; }

  unsigned tick(double now_ms) override {
    unsigned events = pending_events_;
    pending_events_ = 0;
    if (vstream_ < 0 && astream_ < 0) return events;
    if (!started_ && autoplay_) {
      started_ = true;
      play();
      events |= pending_events_;
      pending_events_ = 0;
    }
    if (paused_ || ended_) return events;
    if (now_ms - last_timeupdate_ms_ >= kTimeUpdateIntervalMs) {
      events |= kMediaEventTimeUpdate;
      last_timeupdate_ms_ = now_ms;
    }
    const double t = current_time();
    PumpVideo(t);
    PumpAudio();
    events |= CheckEnded(now_ms);
    return events;
  }

  void invalidate_texture() override {}

 private:
  bool OpenVideo(AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    vctx_ = avcodec_alloc_context3(codec);
    if (!vctx_) return false;
    if (avcodec_parameters_to_context(vctx_, stream->codecpar) < 0) return false;
    vctx_->pkt_timebase = stream->time_base;
    if (avcodec_open2(vctx_, codec, nullptr) < 0) return false;
    return true;
  }

  bool OpenAudio(AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    actx_ = avcodec_alloc_context3(codec);
    if (!actx_) return false;
    if (avcodec_parameters_to_context(actx_, stream->codecpar) < 0) return false;
    actx_->pkt_timebase = stream->time_base;
    if (avcodec_open2(actx_, codec, nullptr) < 0) return false;

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, kAudioChannels);
    if (swr_alloc_set_opts2(&swr_, &out_layout, AV_SAMPLE_FMT_FLT, kAudioRate,
                            &actx_->ch_layout, actx_->sample_fmt, actx_->sample_rate,
                            0, nullptr) < 0 ||
        !swr_ || swr_init(swr_) < 0) {
      return false;
    }
    av_channel_layout_uninit(&out_layout);

    /* Audio is best effort: a headless CI box has no device. Video still
     * plays, and audio is silently dropped. */
    audio_ready_ = audio_ != nullptr && audio_->open();
    if (audio_ready_) ApplyGain();
    return true;
  }

  void ApplyGain() {
    if (audio_ready_) audio_->set_gain(muted_ ? 0.0f : static_cast<float>(volume_));
  }

  double queued_seconds() const {
    if (!audio_ready_) return 0.0;
    return audio_->queued_seconds();
  }

  double stream_pts(const AVStream* stream, int64_t ts) const {
    return static_cast<double>(ts) * av_q2d(stream->time_base);
  }

  void Seek(double seconds) {
    double target = seconds;
    if (duration_ > 0) target = std::min(duration_, std::max(0.0, target));
    if (target < 0) target = 0;
    clock_base_pts_ = target;
    clock_base_ms_ = NowMs();
    ended_ = false;
    started_ = true;

    if (fmt_) {
      const int seek_stream = vstream_ >= 0 ? vstream_ : astream_;
      const int64_t ts = static_cast<int64_t>(target / av_q2d(fmt_->streams[seek_stream]->time_base));
      if (av_seek_frame(fmt_, seek_stream, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        avformat_flush(fmt_);
      }
      if (vctx_) avcodec_flush_buffers(vctx_);
      if (actx_) avcodec_flush_buffers(actx_);
    }
    if (audio_ready_) audio_->clear();
    video_queue_.clear();
    current_valid_ = false;
    drop_before_ = vstream_ >= 0 ? target : -1;
    reader_eof_ = false;
    video_flushed_ = false;
    audio_flushed_ = false;
    video_drained_ = false;
    audio_drained_ = false;
  }

  bool ReadPacket() {
    const int ret = av_read_frame(fmt_, pkt_);
    if (ret < 0) {
      reader_eof_ = true;
      return false;
    }
    if (pkt_->stream_index == vstream_) {
      avcodec_send_packet(vctx_, pkt_);
      DrainVideo();
    } else if (pkt_->stream_index == astream_) {
      avcodec_send_packet(actx_, pkt_);
      DrainAudio();
    }
    av_packet_unref(pkt_);
    return true;
  }

  void DrainVideo() {
    while (avcodec_receive_frame(vctx_, vframe_) == 0) {
      const double pts = stream_pts(fmt_->streams[vstream_], vframe_->best_effort_timestamp);
      Image image;
      ConvertVideo(vframe_, &image);
      video_queue_.push_back({std::move(image), pts});
    }
  }

  void ConvertVideo(const AVFrame* frame, Image* out) {
    const int w = frame->width;
    const int h = frame->height;
    if (!sws_ || sws_width_ != w || sws_height_ != h || sws_pix_fmt_ != frame->format) {
      if (sws_) sws_freeContext(sws_);
      sws_ = sws_getContext(w, h, static_cast<AVPixelFormat>(frame->format), w, h, AV_PIX_FMT_RGBA,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
      sws_width_ = w;
      sws_height_ = h;
      sws_pix_fmt_ = frame->format;
    }
    if (!sws_) return;
    out->width = w;
    out->height = h;
    out->pixels.resize(static_cast<size_t>(w) * h * 4);
    uint8_t* dst[4] = {out->pixels.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {w * 4, 0, 0, 0};
    sws_scale(sws_, frame->data, frame->linesize, 0, h, dst, dst_linesize);
  }

  void DrainAudio() {
    while (avcodec_receive_frame(actx_, aframe_) == 0) {
      if (!audio_ready_) continue;
      const int capacity = swr_get_out_samples(swr_, aframe_->nb_samples);
      if (capacity <= 0) continue;
      audio_buf_.resize(static_cast<size_t>(capacity) * kAudioChannels * sizeof(float));
      uint8_t* out = audio_buf_.data();
      const int got = swr_convert(swr_, &out, capacity,
                                  reinterpret_cast<const uint8_t* const*>(aframe_->extended_data),
                                  aframe_->nb_samples);
      if (got > 0) {
        const int bytes = got * kAudioChannels * static_cast<int>(sizeof(float));
        audio_->write(audio_buf_.data(), static_cast<size_t>(bytes));
      }
    }
  }

  void PumpVideo(double t) {
    if (vstream_ < 0 || video_drained_) return;
    while (true) {
      if (video_queue_.empty()) {
        if (reader_eof_) {
          if (!video_flushed_) {
            avcodec_send_packet(vctx_, nullptr);
            video_flushed_ = true;
            DrainVideo();
            continue;
          }
          video_drained_ = true;
          return;
        }
        if (!ReadPacket() && video_queue_.empty() && reader_eof_) continue;
        if (video_queue_.empty()) continue;
      }
      QueuedFrame& front = video_queue_.front();
      if (drop_before_ >= 0 && front.pts < drop_before_ - 0.001) {
        video_queue_.erase(video_queue_.begin());
        continue;
      }
      if (front.pts <= t + 0.01) {
        current_ = std::move(front.image);
        current_valid_ = true;
        ++generation_;
        video_queue_.erase(video_queue_.begin());
        drop_before_ = -1;
      } else {
        return;
      }
    }
  }

  void PumpAudio() {
    if (astream_ < 0 || audio_drained_) return;
    while (queued_seconds() < kAudioAhead) {
      if (reader_eof_) {
        if (!audio_flushed_) {
          avcodec_send_packet(actx_, nullptr);
          audio_flushed_ = true;
          DrainAudio();
          continue;
        }
        audio_drained_ = true;
        return;
      }
      if (!ReadPacket()) return;
    }
  }

  unsigned CheckEnded(double now_ms) {
    if (ended_) return 0;
    if (!reader_eof_) return 0;
    if (vstream_ >= 0 && !video_drained_) return 0;
    if (astream_ >= 0 && !audio_drained_) return 0;
    if (astream_ >= 0 && queued_seconds() > 0.05) return 0;
    if (vstream_ >= 0 && duration_ > 0 && current_time() < duration_ - 0.05) return 0;
    if (loop_) {
      Seek(0);
      play();
      return kMediaEventPlay;
    }
    ended_ = true;
    paused_ = true;
    clock_base_pts_ = duration_ > 0 ? duration_ : current_time();
    last_timeupdate_ms_ = now_ms;
    return kMediaEventEnded;
  }

  uint64_t id_ = 0;
  std::string source_;
  AVFormatContext* fmt_ = nullptr;
  AVCodecContext* vctx_ = nullptr;
  AVCodecContext* actx_ = nullptr;
  int vstream_ = -1;
  int astream_ = -1;
  AVPacket* pkt_ = nullptr;
  AVFrame* vframe_ = nullptr;
  AVFrame* aframe_ = nullptr;
  SwsContext* sws_ = nullptr;
  SwrContext* swr_ = nullptr;
  int sws_width_ = 0;
  int sws_height_ = 0;
  int sws_pix_fmt_ = -1;

  std::vector<QueuedFrame> video_queue_;
  Image current_;
  bool current_valid_ = false;
  uint64_t generation_ = 0;
  double drop_before_ = -1;

  std::unique_ptr<AudioSink> audio_;
  bool audio_ready_ = false;
  std::vector<uint8_t> audio_buf_;

  bool reader_eof_ = false;
  bool video_flushed_ = false;
  bool audio_flushed_ = false;
  bool video_drained_ = false;
  bool audio_drained_ = false;

  double duration_ = 0;
  double volume_ = 1.0;
  bool muted_ = false;
  bool loop_ = false;
  bool autoplay_ = false;
  bool paused_ = true;
  bool ended_ = false;
  bool started_ = false;
  double clock_base_pts_ = 0;
  double clock_base_ms_ = 0;
  double last_timeupdate_ms_ = -1e9;
  unsigned pending_events_ = 0;
};

}  // namespace

MediaPlayer* OpenMediaPlayer(const char* path, std::string* error,
                             std::unique_ptr<AudioSink> audio) {
  std::unique_ptr<FfmpegPlayer> player(new FfmpegPlayer(std::move(audio)));
  if (!player->Open(path, error)) return nullptr;
  return player.release();
}

MediaPlayer* MediaPlayerById(uint64_t id) {
  auto& table = PlayerTable();
  const auto it = table.find(id);
  return it == table.end() ? nullptr : it->second;
}

void ShutdownMediaPlayers() {
  /* Move the table out before destroying it: `~FfmpegPlayer` erases its own
   * entry, and a destructor mutating the container it is being cleared from
   * would invalidate the clearing loop. With an empty table the erases are
   * no-ops. */
  auto pending = std::move(PlayerTable());
  PlayerTable().clear();
}

}  // namespace xtgui

extern "C" XtMediaPlayerHandle xt_media_backend_open(const char* path, char* error,
                                                     size_t error_size, void* audio_sink) {
  std::unique_ptr<xtgui::AudioSink> audio(static_cast<xtgui::AudioSink*>(audio_sink));
  std::string message;
  xtgui::MediaPlayer* player = xtgui::OpenMediaPlayer(path != nullptr ? path : "", &message, std::move(audio));
  if (player == nullptr && error != nullptr && error_size > 0) {
    std::snprintf(error, error_size, "%s", message.c_str());
  }
  return player;
}

extern "C" XtMediaPlayerHandle xt_media_backend_by_id(uint64_t id) {
  return xtgui::MediaPlayerById(id);
}

extern "C" void xt_media_backend_shutdown(void) { xtgui::ShutdownMediaPlayers(); }
