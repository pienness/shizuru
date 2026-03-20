#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "io/tts/tts_device.h"
#include "tts/elevenlabs/elevenlabs_client.h"
#include "tts/tts_client.h"
#include "tts/config.h"

namespace shizuru::services {

// ElevenLabs implementation of TtsDevice.
// Wraps ElevenLabsClient: accepts text/plain DataFrames on "text_in",
// emits audio/pcm DataFrames on "audio_out".
class ElevenLabsTtsDevice : public io::TtsDevice {
 public:
  // Production constructor: creates ElevenLabsClient from config.
  explicit ElevenLabsTtsDevice(ElevenLabsConfig config,
                                std::string device_id = "elevenlabs_tts");

  // Test constructor: inject any TtsClient (e.g. a mock).
  ElevenLabsTtsDevice(std::unique_ptr<TtsClient> client,
                      std::string device_id);

  // IoDevice interface
  std::string GetDeviceId() const override;
  std::vector<io::PortDescriptor> GetPortDescriptors() const override;
  void OnInput(const std::string& port_name, io::DataFrame frame) override;
  void SetOutputCallback(io::OutputCallback cb) override;
  void Start() override;
  void Stop() override;

  // TtsDevice interface
  void CancelSynthesis() override;

 private:
  void Synthesize(const std::string& text);

  static constexpr char kTextIn[]   = "text_in";
  static constexpr char kAudioOut[] = "audio_out";

  std::string device_id_;
  std::unique_ptr<TtsClient> client_;  // owned, polymorphic
  std::atomic<bool> active_{false};

  mutable std::mutex output_cb_mutex_;
  io::OutputCallback output_cb_;

  // Synthesis runs on a background thread to avoid blocking OnInput.
  std::mutex synth_mutex_;
  std::thread synth_thread_;
};

}  // namespace shizuru::services
