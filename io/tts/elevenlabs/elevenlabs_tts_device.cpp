#include "elevenlabs_tts_device.h"

#include <chrono>
#include <utility>

#include "async_logger.h"

namespace shizuru::io {

ElevenLabsTtsDevice::ElevenLabsTtsDevice(services::ElevenLabsConfig config,
                                         std::string device_id)
    : device_id_(std::move(device_id)),
      client_(std::make_unique<services::ElevenLabsClient>(std::move(config))) {}

ElevenLabsTtsDevice::ElevenLabsTtsDevice(std::unique_ptr<services::TtsClient> client,
                                         std::string device_id)
    : device_id_(std::move(device_id)), client_(std::move(client)) {}

std::string ElevenLabsTtsDevice::GetDeviceId() const { return device_id_; }

std::vector<PortDescriptor> ElevenLabsTtsDevice::GetPortDescriptors() const {
  return {
      {kTextIn,   PortDirection::kInput,  "text/plain"},
      {kAudioOut, PortDirection::kOutput, "audio/pcm"},
  };
}

void ElevenLabsTtsDevice::OnInput(const std::string& port_name, DataFrame frame) {
  if (!active_.load()) { return; }
  if (port_name != kTextIn) {
    LOG_WARN("ElevenLabsTtsDevice: unsupported input port: {}", port_name);
    return;
  }
  const std::string text(frame.payload.begin(), frame.payload.end());
  if (text.empty()) { return; }

  std::lock_guard<std::mutex> lock(synth_mutex_);
  if (synth_thread_.joinable()) { synth_thread_.join(); }
  synth_thread_ = std::thread([this, text] { Synthesize(text); });
}

void ElevenLabsTtsDevice::SetOutputCallback(OutputCallback cb) {
  std::lock_guard<std::mutex> lock(output_cb_mutex_);
  output_cb_ = std::move(cb);
}

void ElevenLabsTtsDevice::Start() { active_.store(true); }

void ElevenLabsTtsDevice::Stop() {
  active_.store(false);
  client_->Cancel();
  std::lock_guard<std::mutex> lock(synth_mutex_);
  if (synth_thread_.joinable()) { synth_thread_.join(); }
}

void ElevenLabsTtsDevice::CancelSynthesis() { Stop(); }

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void ElevenLabsTtsDevice::Synthesize(const std::string& text) {
  // Carry buffer: holds at most 1 leftover byte from the previous chunk.
  // s16le PCM requires 2-byte alignment; HTTP chunks may arrive with an odd
  // byte count, splitting a sample across two consecutive callbacks.
  // Pattern mirrors elevenlabs_tts_playout.cpp: stitch carry first, then
  // process the bulk, then stash any trailing odd byte.
  uint8_t carry     = 0;
  bool    has_carry = false;

  auto emit = [&](const uint8_t* buf, size_t byte_count) {
    if (byte_count == 0) { return; }
    DataFrame frame;
    frame.type          = "audio/pcm";
    frame.payload.assign(buf, buf + byte_count);
    frame.source_device = device_id_;
    frame.source_port   = kAudioOut;
    frame.timestamp     = std::chrono::steady_clock::now();
    OutputCallback cb;
    {
      std::lock_guard<std::mutex> lock(output_cb_mutex_);
      cb = output_cb_;
    }
    if (cb) { cb(device_id_, kAudioOut, std::move(frame)); }
  };

  try {
    client_->Synthesize(text, [&](const void* data, size_t bytes) {
      if (!active_.load() || bytes == 0) { return; }

      const auto* src = static_cast<const uint8_t*>(data);
      size_t offset = 0;

      // Step 1: if we have a carry byte, stitch it with src[0] to complete
      // the sample, emit that single aligned pair, then advance past it.
      if (has_carry) {
        uint8_t pair[2] = {carry, src[0]};
        emit(pair, 2);
        has_carry = false;
        offset = 1;
      }

      // Step 2: emit the aligned bulk of the remaining bytes.
      const size_t remaining = bytes - offset;
      const size_t aligned   = (remaining / sizeof(int16_t)) * sizeof(int16_t);
      emit(src + offset, aligned);

      // Step 3: stash any trailing odd byte for the next chunk.
      if (remaining % sizeof(int16_t) != 0) {
        carry     = src[offset + aligned];
        has_carry = true;
      }
    });
  } catch (const std::exception& e) {
    LOG_ERROR("ElevenLabsTtsDevice: synthesis error: {}", e.what());
  }
}

}  // namespace shizuru::io
