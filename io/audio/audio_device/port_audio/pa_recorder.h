#pragma once

#include <portaudio.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdint>
#include "audio_device/audio_recorder.h"
#include "audio_device/audio_buffer.h"
#include "audio_device/port_audio/pa_init.h"

namespace shizuru::io {

class PaRecorder : public AudioRecorder {
 public:
  explicit PaRecorder(const RecorderConfig& config = {})
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

  ~PaRecorder() override { Stop(); }

  PaRecorder(const PaRecorder&) = delete;
  PaRecorder& operator=(const PaRecorder&) = delete;

  void Start() override {
    if (recording_) return;

    PaStreamParameters params{};
    params.device = (config_.device_id < 0) ? Pa_GetDefaultInputDevice()
                                            : config_.device_id;
    if (params.device == paNoDevice) {
      throw std::runtime_error("No input device available");
    }
    params.channelCount = static_cast<int>(config_.channels);
    params.sampleFormat = ToPaFormat(config_.format);
    params.suggestedLatency =
        Pa_GetDeviceInfo(params.device)->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream_, &params, nullptr,
                                config_.sample_rate,
                                config_.frames_per_buffer,
                                paClipOff, PaCallback, this);
    if (err != paNoError) {
      throw std::runtime_error(
          std::string("Failed to open input stream: ") + Pa_GetErrorText(err));
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
      Pa_CloseStream(stream_);
      stream_ = nullptr;
      throw std::runtime_error(
          std::string("Failed to start input stream: ") + Pa_GetErrorText(err));
    }
    recording_ = true;
  }

  void Stop() override {
    if (!recording_ || !stream_) return;
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    recording_ = false;
  }

  bool IsRecording() const override { return recording_; }

  size_t Read(void* data, size_t frame_count) override {
    if (config_.format == SampleFormat::kFloat32) {
      return buf_f32_->Read(static_cast<float*>(data), frame_count);
    }
    return buf_s16_->Read(static_cast<int16_t*>(data), frame_count);
  }

  void SetDataCallback(DataCallback cb) override {
    data_callback_ = std::move(cb);
  }

 private:
  // Map SampleFormat enum to PortAudio's format constant.
  static unsigned long ToPaFormat(SampleFormat fmt) {
    if (fmt == SampleFormat::kFloat32) return static_cast<unsigned long>(paFloat32);
    return static_cast<unsigned long>(paInt16);
  }

  static int PaCallback(const void* input, void* /*output*/,
                        unsigned long frame_count,
                        const PaStreamCallbackTimeInfo* /*time_info*/,
                        PaStreamCallbackFlags /*flags*/, void* user_data) {
    auto* self = static_cast<PaRecorder*>(user_data);
    if (!input) return paContinue;

    if (self->config_.format == SampleFormat::kFloat32) {
      auto* in = static_cast<const float*>(input);
      self->buf_f32_->Write(in, frame_count);
    } else {
      auto* in = static_cast<const int16_t*>(input);
      self->buf_s16_->Write(in, frame_count);
    }

    if (self->data_callback_) {
      self->data_callback_(input, frame_count);
    }
    return paContinue;
  }

  RecorderConfig config_;
  PaStream* stream_ = nullptr;
  bool recording_ = false;
  std::unique_ptr<AudioBuffer<float>> buf_f32_;
  std::unique_ptr<AudioBuffer<int16_t>> buf_s16_;
  DataCallback data_callback_;
};

}  // namespace shizuru::io
