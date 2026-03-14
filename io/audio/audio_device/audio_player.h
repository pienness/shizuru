#pragma once

#include <cstddef>
#include "audio_device/sample_format.h"

namespace shizuru::io {

struct PlayerConfig {
  int device_id = -1;       // -1 = default output
  double sample_rate = 16000.0;
  size_t channels = 1;
  size_t frames_per_buffer = 480;  // 30ms at 16kHz
  size_t buffer_capacity_frames = 16000;  // 1s ring buffer
  SampleFormat format = SampleFormat::kInt16;
};

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;

  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual bool IsPlaying() const = 0;

  // Write frames from caller-provided buffer.
  virtual size_t Write(const void* data, size_t frame_count) = 0;

  virtual size_t Buffered() const = 0;
};

}  // namespace shizuru::io
