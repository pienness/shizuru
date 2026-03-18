#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace shizuru::io {

// Lock-free single-producer single-consumer ring buffer.
// Unit: samples per channel. T = int16_t (s16le only).
template <typename T>
class AudioBuffer {
 public:
  // capacity_samples: ring buffer capacity in samples per channel.
  explicit AudioBuffer(size_t capacity_samples, size_t channels = 1)
      : channels_(channels), capacity_samples_(capacity_samples) {
    buffer_.resize(capacity_samples_ * channels_);
  }

  // Write up to sample_count samples per channel from data.
  // Returns number of samples per channel actually written.
  size_t Write(const T* data, size_t sample_count) {
    const size_t available = AvailableWrite();
    const size_t to_write  = std::min(sample_count, available);
    if (to_write == 0) { return 0; }

    const size_t wp      = write_pos_.load(std::memory_order_relaxed);
    const size_t n_elems = to_write * channels_;

    for (size_t i = 0; i < n_elems; ++i) {
      buffer_[((wp * channels_) + i) % buffer_.size()] = data[i];
    }

    write_pos_.store((wp + to_write) % capacity_samples_,
                     std::memory_order_release);
    return to_write;
  }

  // Read up to sample_count samples per channel into data.
  // Returns number of samples per channel actually read.
  size_t Read(T* data, size_t sample_count) {
    const size_t available = AvailableRead();
    const size_t to_read   = std::min(sample_count, available);
    if (to_read == 0) { return 0; }

    const size_t rp      = read_pos_.load(std::memory_order_relaxed);
    const size_t n_elems = to_read * channels_;

    for (size_t i = 0; i < n_elems; ++i) {
      data[i] = buffer_[((rp * channels_) + i) % buffer_.size()];
    }

    read_pos_.store((rp + to_read) % capacity_samples_,
                    std::memory_order_release);
    return to_read;
  }

  // Returns samples per channel available to read.
  size_t AvailableRead() const {
    const size_t wp = write_pos_.load(std::memory_order_acquire);
    const size_t rp = read_pos_.load(std::memory_order_acquire);
    if (wp >= rp) { return wp - rp; }
    return capacity_samples_ - rp + wp;
  }

  // Returns samples per channel available to write.
  size_t AvailableWrite() const {
    return capacity_samples_ - 1 - AvailableRead();
  }

  void Reset() {
    read_pos_.store(0, std::memory_order_release);
    write_pos_.store(0, std::memory_order_release);
  }

 private:
  std::vector<T>       buffer_;
  size_t               channels_;
  size_t               capacity_samples_;
  std::atomic<size_t>  read_pos_{0};
  std::atomic<size_t>  write_pos_{0};
};

}  // namespace shizuru::io
