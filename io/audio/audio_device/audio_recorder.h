#pragma once

#include <cstddef>
#include <functional>
#include "audio_device/audio_frame.h"

namespace shizuru::io {

struct RecorderConfig {
  int    device_id            = -1;    // -1 = default input
  int    sample_rate          = 16000;
  size_t channel_count        = 1;
  size_t frames_per_buffer    = 320;   // 20ms at 16kHz
  size_t buffer_capacity_samples = 16000; // 1s ring buffer at 16kHz
};

class AudioRecorder {
 public:
  // Callback invoked on each captured AudioFrame (from the audio thread).
  using FrameCallback = std::function<void(const AudioFrame&)>;

  virtual ~AudioRecorder() = default;

  virtual void Start() = 0;
  virtual void Stop() = 0;
  [[nodiscard]] virtual bool IsRecording() const = 0;

  // Pull one frame from the internal ring buffer into the caller's frame.
  // Returns the number of samples per channel actually read.
  virtual size_t Read(AudioFrame& frame) = 0;

  virtual void SetFrameCallback(FrameCallback cb) = 0;
};

}  // namespace shizuru::io
