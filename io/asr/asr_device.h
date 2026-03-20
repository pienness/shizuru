#pragma once

#include "io/io_device.h"

namespace shizuru::io {

// Vendor-agnostic ASR device interface.
// Accepts audio DataFrames on "audio_in", emits text DataFrames on "text_out".
//
// Port contract:
//   Input  "audio_in" — accepts DataFrames with type "audio/pcm"
//   Output "text_out" — emits DataFrames with type "text/plain"
class AsrDevice : public IoDevice {
 public:
  // Cancels any in-progress transcription immediately.
  virtual void CancelTranscription() = 0;
};

}  // namespace shizuru::io
