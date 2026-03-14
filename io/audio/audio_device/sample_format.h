#pragma once

#include <cstddef>

namespace shizuru::io {

enum class SampleFormat {
  kInt16,    // s16le, 2 bytes per sample
  kFloat32,  // 32-bit float, 4 bytes per sample
};

inline size_t BytesPerSample(SampleFormat fmt) {
  switch (fmt) {
    case SampleFormat::kInt16:   return 2;
    case SampleFormat::kFloat32: return 4;
  }
  return 0;
}

}  // namespace shizuru::io
