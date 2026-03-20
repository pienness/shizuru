#pragma once

#include "io/io_device.h"

namespace shizuru::io {

// Vendor-agnostic TTS device interface.
// Accepts text DataFrames on "text_in", emits audio DataFrames on "audio_out".
//
// Port contract:
//   Input  "text_in"  — accepts DataFrames with type "text/plain"
//   Output "audio_out" — emits DataFrames with type "audio/pcm"
class TtsDevice : public IoDevice {
 public:
  // Cancels any in-progress synthesis immediately.
  virtual void CancelSynthesis() = 0;
};

}  // namespace shizuru::io
