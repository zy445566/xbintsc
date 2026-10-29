/*
 * xbintsc GUI engine — the SDL playback device behind an AudioContext.
 *
 * The mixer renders on demand (`AudioMixer::render`); this unit is the only
 * place that opens a device and pumps it. SDL calls `stream_callback` on its
 * audio thread, which renders the requested amount into the stream. A headless
 * host simply has no device: `start_device` fails once and playback stays
 * silent, while the graph and its clock remain usable (the offline render hook
 * exercises the same code path).
 */

#include "audio_mixer.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

namespace xtgui {

namespace {

void SDLCALL stream_callback(void *userdata, SDL_AudioStream *stream, int additional_amount,
                             int total_amount) {
  (void)total_amount;
  if (additional_amount <= 0) return;
  int frames = additional_amount / static_cast<int>(sizeof(float) * kAudioMixChannels);
  if (frames <= 0) return;
  std::vector<float> buffer(static_cast<size_t>(frames) * kAudioMixChannels);
  static_cast<AudioMixer *>(userdata)->render(buffer.data(), frames);
  SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(buffer.size() * sizeof(float)));
}

}  // namespace

bool AudioMixer::start_device() {
  if (offline_) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return false;
  if (stream_ != nullptr) return true;
  if (device_tried_) return false;
  device_tried_ = true;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    /* A missing device is not fatal (the page keeps its graph and clock), but
     * silently staying quiet is indistinguishable from a broken mixer. */
    std::fprintf(stderr, "xt_gui: audio subsystem unavailable: %s\n", SDL_GetError());
    return false;
  }
  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_F32;
  spec.channels = kAudioMixChannels;
  spec.freq = kAudioMixRate;
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, stream_callback,
                                      this);
  if (stream_ == nullptr) {
    std::fprintf(stderr, "xt_gui: could not open the default audio device: %s\n",
                 SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void AudioMixer::suspend_device() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  if (stream_ != nullptr) SDL_PauseAudioStreamDevice(stream_);
}

void AudioMixer::resume_device() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = true;
  }
  if (start_device()) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_ != nullptr) SDL_ResumeAudioStreamDevice(stream_);
  }
}

void AudioMixer::close_device() {
  SDL_AudioStream *stream = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stream = stream_;
    stream_ = nullptr;
    running_ = false;
  }
  if (stream != nullptr) SDL_DestroyAudioStream(stream);
}

bool AudioMixer::has_device() {
  std::lock_guard<std::mutex> lock(mutex_);
  return stream_ != nullptr;
}

}  // namespace xtgui
