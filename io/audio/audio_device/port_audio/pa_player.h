#pragma once

#include <portaudio.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cstdint>
#include "audio_device/audio_player.h"
#include "audio_device/audio_buffer.h"
#include "audio_device/port_audio/pa_init.h"

namespace shizuru::io {

class PaPlayer : public AudioPlayer {
 public:
  explicit PaPlayer(const PlayerConfig& config = {})
      : config_(config) {
    EnsurePaInitialized();
    if (config_.format == SampleFormat::kFloat32) {
      buf_f32_ = std::make_unique<AudioBuffer<float>>(
          config_.buffer_capacity_frames, config_.channels);
    } else {
      buf_s16_ = std::make_unique<AudioBuffer<int16_t>>(
          config_.buffer_capacity_frames, config_.channels);
    }
  }

  ~PaPlayer() override { Stop(); }

  PaPlayer(const PaPlayer&) = delete;
  PaPlayer& operator=(const PaPlayer&) = delete;

  void Start() override {
    if (playing_) return;

    PaStreamParameters params{};
    params.device = (config_.device_id < 0) ? Pa_GetDefaultOutputDevice()
                                            : config_.device_id;
    if (params.device == paNoDevice) {
      throw std::runtime_error("No output device available");
    }
    params.channelCount = static_cast<int>(config_.channels);
    params.sampleFormat = ToPaFormat(config_.format);
    params.suggestedLatency =
        Pa_GetDeviceInfo(params.device)->defaultLowOutputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_, nullptr, &params,
                                config_.sample_rate,
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
    if (!playing_ || !stream_) return;
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    playing_ = false;
  }

  bool IsPlaying() const override { return playing_; }

  size_t Write(const void* data, size_t frame_count) override {
    if (config_.format == SampleFormat::kFloat32) {
      return buf_f32_->Write(static_cast<const float*>(data), frame_count);
    }
    return buf_s16_->Write(static_cast<const int16_t*>(data), frame_count);
  }

  size_t Buffered() const override {
    if (config_.format == SampleFormat::kFloat32) {
      return buf_f32_->AvailableRead();
    }
    return buf_s16_->AvailableRead();
  }

 private:
  // Map SampleFormat enum to PortAudio's format constant.
  static unsigned long ToPaFormat(SampleFormat fmt) {
    if (fmt == SampleFormat::kFloat32) return static_cast<unsigned long>(paFloat32);
    return static_cast<unsigned long>(paInt16);
  }

  static int PaCallback(const void* /*input*/, void* output,
                        unsigned long frame_count,
                        const PaStreamCallbackTimeInfo* /*time_info*/,
                        PaStreamCallbackFlags /*flags*/, void* user_data) {
    auto* self = static_cast<PaPlayer*>(user_data);
    size_t read = 0;

    if (self->config_.format == SampleFormat::kFloat32) {
      auto* out = static_cast<float*>(output);
      read = self->buf_f32_->Read(out, frame_count);
      if (read < frame_count) {
        size_t silence = (frame_count - read) * self->config_.channels;
        std::memset(out + read * self->config_.channels, 0,
                    silence * sizeof(float));
      }
    } else {
      auto* out = static_cast<int16_t*>(output);
      read = self->buf_s16_->Read(out, frame_count);
      if (read < frame_count) {
        size_t silence = (frame_count - read) * self->config_.channels;
        std::memset(out + read * self->config_.channels, 0,
                    silence * sizeof(int16_t));
      }
    }
    return paContinue;
  }

  PlayerConfig config_;
  PaStream* stream_ = nullptr;
  bool playing_ = false;
  std::unique_ptr<AudioBuffer<float>> buf_f32_;
  std::unique_ptr<AudioBuffer<int16_t>> buf_s16_;
};

}  // namespace shizuru::io
