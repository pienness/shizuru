#pragma once

#include <cstring>
#include <stdexcept>
#include <string>

#include <portaudio.h>

#include "audio_device/audio_buffer.h"
#include "audio_device/audio_frame.h"
#include "audio_device/audio_player.h"
#include "audio_device/port_audio/pa_init.h"

namespace shizuru::io {

class PaPlayer : public AudioPlayer {
 public:
  explicit PaPlayer(const PlayerConfig& config = {})
      : config_(config),
        buf_(config.buffer_capacity_samples, config.channel_count) {
    EnsurePaInitialized();
  }

  ~PaPlayer() override { Stop(); }

  PaPlayer(const PaPlayer&) = delete;
  PaPlayer& operator=(const PaPlayer&) = delete;

  void Start() override {
    if (playing_) { return; }

    PaStreamParameters params{};
    params.device = (config_.device_id < 0) ? Pa_GetDefaultOutputDevice()
                                            : config_.device_id;
    if (params.device == paNoDevice) {
      throw std::runtime_error("No output device available");
    }
    params.channelCount          = static_cast<int>(config_.channel_count);
    params.sampleFormat          = paInt16;
    params.suggestedLatency      =
        Pa_GetDeviceInfo(params.device)->defaultLowOutputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_, nullptr, &params,
                                static_cast<double>(config_.sample_rate),
                                config_.frames_per_buffer,
                                paClipOff, PaCallback, this);
    if (err != paNoError) {
      throw std::runtime_error(
          std::string("Failed to open output stream: ") + Pa_GetErrorText(err));
    }
    err = Pa_StartStream(stream_);
    if (err != paNoError) {
      Pa_CloseStream(stream_);
      stream_ = nullptr;
      throw std::runtime_error(
          std::string("Failed to start output stream: ") + Pa_GetErrorText(err));
    }
    playing_ = true;
  }

  void Stop() override {
    if (!playing_ || stream_ == nullptr) { return; }
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_  = nullptr;
    playing_ = false;
  }

  [[nodiscard]] bool IsPlaying() const override { return playing_; }

  size_t Write(const AudioFrame& frame) override {
    return buf_.Write(frame.data, frame.sample_count);
  }

  [[nodiscard]] size_t Buffered() const override { return buf_.AvailableRead(); }

 private:
  static int PaCallback(const void* /*input*/, void* output,
                        unsigned long frame_count,
                        const PaStreamCallbackTimeInfo* /*time_info*/,
                        PaStreamCallbackFlags /*flags*/, void* user_data) {
    auto* self = static_cast<PaPlayer*>(user_data);
    auto* out  = static_cast<int16_t*>(output);

    const size_t read = self->buf_.Read(out, frame_count);
    if (read < frame_count) {
      // Underrun — fill remainder with silence.
      std::memset(out + read * self->config_.channel_count, 0,
                  (frame_count - read) * self->config_.channel_count *
                      sizeof(int16_t));
    }
    return paContinue;
  }

  PlayerConfig              config_;
  PaStream*                 stream_  = nullptr;
  bool                      playing_ = false;
  AudioBuffer<int16_t>      buf_;
};

}  // namespace shizuru::io
