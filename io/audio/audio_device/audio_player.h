#pragma once

#include <cstddef>
#include "audio_device/audio_frame.h"

namespace shizuru::io {

struct PlayerConfig {
  int    device_id            = -1;    // -1 = default output
  int    sample_rate          = 16000;
  size_t channel_count        = 1;
  size_t frames_per_buffer    = 320;   // 20ms at 16kHz
  size_t buffer_capacity_samples = 16000; // 1s ring buffer at 16kHz
};

class AudioPlayer {
 public:
  virtual ~AudioPlayer() = default;

  virtual void Start() = 0;
  virtual void Stop() = 0;
  [[nodiscard]] virtual bool IsPlaying() const = 0;

  // Write one audio frame into the playout buffer.
  // Returns the number of samples per channel actually written.
  virtual size_t Write(const AudioFrame& frame) = 0;

  // Number of frames currently buffered (samples per channel).
  [[nodiscard]] virtual size_t Buffered() const = 0;
};

}  // namespace shizuru::io
