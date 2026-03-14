#pragma once

#include <cstddef>
#include <functional>
#include "audio_device/sample_format.h"

namespace shizuru::io {

struct RecorderConfig {
  int device_id = -1;       // -1 = default input
  double sample_rate = 16000.0;
  size_t channels = 1;
  size_t frames_per_buffer = 480;  // 30ms at 16kHz
  size_t buffer_capacity_frames = 16000;  // 1s ring buffer
  SampleFormat format = SampleFormat::kInt16;
};

class AudioRecorder {
 public:
  using DataCallback = std::function<void(const void*, size_t)>;

  virtual ~AudioRecorder() = default;

  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual bool IsRecording() const = 0;

  // Read frames into caller-provided buffer. Caller must ensure
  // the buffer is large enough: frame_count * channels * BytesPerSample(format).
  virtual size_t Read(void* data, size_t frame_count) = 0;

  virtual void SetDataCallback(DataCallback cb) = 0;
};

}  // namespace shizuru::io
