#pragma once

#include <cstddef>
#include <cstdint>

namespace shizuru::io {

// Maximum supported parameters for stack allocation sizing.
// 20ms @ 48kHz stereo = 960 samples/ch × 2 ch = 1920 int16_t = 3840 bytes.
inline constexpr size_t kMaxSampleRate      = 48000;
inline constexpr size_t kMaxChannels        = 2;
inline constexpr size_t kFrameDurationMs    = 20;
inline constexpr size_t kMaxSamplesPerFrame =
    (kMaxSampleRate / 1000U) * kFrameDurationMs * kMaxChannels;  // 1920

// A fixed-duration audio frame of s16le PCM samples, stored on the stack.
// All audio in the system is normalized to this format:
//   - Encoding:  signed 16-bit little-endian (s16le)
//   - Duration:  up to kFrameDurationMs ms
//   - Layout:    interleaved channels (L0 R0 L1 R1 ...)
//
// Copyable and trivially movable — safe to pass by value across threads
// and into lock-free queues.
struct AudioFrame {
  int    sample_rate   = 16000;  // Hz
  size_t channel_count = 1;
  size_t sample_count  = 0;      // samples per channel in this frame

  // Interleaved s16le samples. Total valid elements = sample_count * channel_count.
  int16_t data[kMaxSamplesPerFrame] = {};

  // Total number of valid int16_t elements in data[].
  [[nodiscard]] size_t NumSamples() const { return sample_count * channel_count; }

  // Duration of this frame in milliseconds.
  [[nodiscard]] double DurationMs() const {
    if (sample_rate == 0) { return 0.0; }
    return static_cast<double>(sample_count) * 1000.0 /
           static_cast<double>(sample_rate);
  }
};

}  // namespace shizuru::io
