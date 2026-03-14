#pragma once

#include <cstddef>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstring>

namespace shizuru::io {

// Lock-free single-producer single-consumer ring buffer for audio frames.
// T = sample type (float, int16_t, etc.)
template <typename T>
class AudioBuffer {
 public:
  explicit AudioBuffer(size_t capacity_frames, size_t channels = 1)
      : channels_(channels), capacity_(capacity_frames) {
    buffer_.resize(capacity_ * channels_);
  }

  // Returns number of frames actually written.
  size_t Write(const T* data, size_t frame_count) {
    size_t available = AvailableWrite();
    size_t to_write = std::min(frame_count, available);
    if (to_write == 0) return 0;

    size_t wp = write_pos_.load(std::memory_order_relaxed);
    size_t samples = to_write * channels_;

    for (size_t i = 0; i < samples; ++i) {
      buffer_[((wp * channels_) + i) % buffer_.size()] = data[i];
    }

    write_pos_.store((wp + to_write) % capacity_, std::memory_order_release);
    return to_write;
  }

  // Returns number of frames actually read.
  size_t Read(T* data, size_t frame_count) {
    size_t available = AvailableRead();
    size_t to_read = std::min(frame_count, available);
    if (to_read == 0) return 0;

    size_t rp = read_pos_.load(std::memory_order_relaxed);
    size_t samples = to_read * channels_;

    for (size_t i = 0; i < samples; ++i) {
      data[i] = buffer_[((rp * channels_) + i) % buffer_.size()];
    }

    read_pos_.store((rp + to_read) % capacity_, std::memory_order_release);
    return to_read;
  }

  size_t AvailableRead() const {
    size_t wp = write_pos_.load(std::memory_order_acquire);
    size_t rp = read_pos_.load(std::memory_order_acquire);
    if (wp >= rp) return wp - rp;
    return capacity_ - rp + wp;
  }

  size_t AvailableWrite() const {
    return capacity_ - 1 - AvailableRead();
  }

  void Reset() {
    read_pos_.store(0, std::memory_order_release);
    write_pos_.store(0, std::memory_order_release);
  }

 private:
  std::vector<T> buffer_;
  size_t channels_;
  size_t capacity_;
  std::atomic<size_t> read_pos_{0};
  std::atomic<size_t> write_pos_{0};
};

}  // namespace shizuru::io
