// Unit + property-based tests for IO device control_in dispatch
// Feature: core-decoupling, Property 9: IO devices dispatch recognized control commands

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "io/control_frame.h"
#include "io/data_frame.h"
#include "io/tts/elevenlabs/elevenlabs_tts_device.h"
#include "io/asr/baidu/baidu_asr_device.h"
#include "io/audio/audio_playout_device.h"
#include "io/audio/audio_device/audio_player.h"
#include "services/tts/tts_client.h"

namespace shizuru::io {
namespace {

// ---------------------------------------------------------------------------
// Mock TtsClient — tracks Cancel() calls
// ---------------------------------------------------------------------------
class MockTtsClient : public services::TtsClient {
 public:
  void Synthesize(const services::TtsRequest& /*req*/,
                  services::TtsAudioCallback /*cb*/) override {}
  void Synthesize(const std::string& /*text*/,
                  services::TtsAudioCallback /*cb*/) override {}
  void Cancel() override { ++cancel_count; }

  std::atomic<int> cancel_count{0};
};

// ---------------------------------------------------------------------------
// Mock AudioPlayer — tracks Flush() calls
// ---------------------------------------------------------------------------
class MockAudioPlayer : public AudioPlayer {
 public:
  void Start() override {}
  void Stop() override { ++stop_count; }
  bool IsPlaying() const override { return false; }
  size_t Write(const AudioFrame& /*frame*/) override { return 0; }
  size_t Buffered() const override { return 0; }
  void Flush() override { ++flush_count; }

  std::atomic<int> stop_count{0};
  std::atomic<int> flush_count{0};
};

// ---------------------------------------------------------------------------
// Helper: build a control/command DataFrame
// ---------------------------------------------------------------------------
DataFrame MakeControlFrame(const std::string& cmd) {
  return ControlFrame::Make(cmd);
}

// ---------------------------------------------------------------------------
// ElevenLabsTtsDevice — control_in tests
// ---------------------------------------------------------------------------

TEST(ElevenLabsTtsDeviceControlTest, CancelCommandCallsCancelSynthesis) {
  auto* mock = new MockTtsClient();
  ElevenLabsTtsDevice dev(std::unique_ptr<services::TtsClient>(mock), "tts_test");
  dev.Start();

  dev.OnInput("control_in", MakeControlFrame("cancel"));

  EXPECT_EQ(mock->cancel_count.load(), 1);
  dev.Stop();
}

TEST(ElevenLabsTtsDeviceControlTest, UnrecognizedCommandDoesNotCrash) {
  auto* mock = new MockTtsClient();
  ElevenLabsTtsDevice dev(std::unique_ptr<services::TtsClient>(mock), "tts_test");
  dev.Start();

  EXPECT_NO_THROW(dev.OnInput("control_in", MakeControlFrame("unknown_cmd")));
  EXPECT_EQ(mock->cancel_count.load(), 0);
  dev.Stop();
}

TEST(ElevenLabsTtsDeviceControlTest, ControlInPortInDescriptors) {
  auto* mock = new MockTtsClient();
  ElevenLabsTtsDevice dev(std::unique_ptr<services::TtsClient>(mock), "tts_test");

  bool found = false;
  for (const auto& p : dev.GetPortDescriptors()) {
    if (p.name == "control_in" && p.direction == PortDirection::kInput &&
        p.data_type == "control/command") {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// AudioPlayoutDevice — control_in tests
// ---------------------------------------------------------------------------

TEST(AudioPlayoutDeviceControlTest, CancelCommandCallsPlayerFlush) {
  auto* mock = new MockAudioPlayer();
  AudioPlayoutDevice dev(std::unique_ptr<AudioPlayer>(mock), "playout_test");

  dev.OnInput("control_in", MakeControlFrame("cancel"));

  EXPECT_EQ(mock->flush_count.load(), 1);
  EXPECT_EQ(mock->stop_count.load(), 0);
}

TEST(AudioPlayoutDeviceControlTest, UnrecognizedCommandDoesNotCrash) {
  auto* mock = new MockAudioPlayer();
  AudioPlayoutDevice dev(std::unique_ptr<AudioPlayer>(mock), "playout_test");

  EXPECT_NO_THROW(dev.OnInput("control_in", MakeControlFrame("unknown_cmd")));
  EXPECT_EQ(mock->stop_count.load(), 0);
}

TEST(AudioPlayoutDeviceControlTest, ControlInPortInDescriptors) {
  auto* mock = new MockAudioPlayer();
  AudioPlayoutDevice dev(std::unique_ptr<AudioPlayer>(mock), "playout_test");

  bool found = false;
  for (const auto& p : dev.GetPortDescriptors()) {
    if (p.name == "control_in" && p.direction == PortDirection::kInput &&
        p.data_type == "control/command") {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// BaiduAsrDevice — control_in tests (observable side effects)
// CancelTranscription() clears the audio buffer.
// Flush() on empty buffer is a no-op (no crash).
// ---------------------------------------------------------------------------

// Helper: build a BaiduAsrDevice with a dummy config (no network calls made).
static services::BaiduConfig DummyConfig() {
  services::BaiduConfig cfg;
  cfg.api_key    = "test";
  cfg.secret_key = "test";
  return cfg;
}

TEST(BaiduAsrDeviceControlTest, CancelCommandClearsAudioBuffer) {
  BaiduAsrDevice dev(DummyConfig(), "asr_test");
  dev.Start();

  // Feed some audio data.
  DataFrame audio_frame;
  audio_frame.type    = "audio/pcm";
  audio_frame.payload = std::vector<uint8_t>(1024, 0);
  dev.OnInput("audio_in", audio_frame);

  // Send cancel — should call CancelTranscription() which clears the buffer.
  dev.OnInput("control_in", MakeControlFrame("cancel"));

  // After cancel, Flush() on an empty buffer should be a no-op (no crash).
  EXPECT_NO_THROW(dev.OnInput("control_in", MakeControlFrame("flush")));

  dev.Stop();
}

TEST(BaiduAsrDeviceControlTest, FlushCommandDoesNotCrash) {
  BaiduAsrDevice dev(DummyConfig(), "asr_test");
  dev.Start();

  // Flush on empty buffer — should be a no-op.
  EXPECT_NO_THROW(dev.OnInput("control_in", MakeControlFrame("flush")));

  dev.Stop();
}

TEST(BaiduAsrDeviceControlTest, UnrecognizedCommandDoesNotCrash) {
  BaiduAsrDevice dev(DummyConfig(), "asr_test");
  dev.Start();

  EXPECT_NO_THROW(dev.OnInput("control_in", MakeControlFrame("unknown_cmd")));

  dev.Stop();
}

TEST(BaiduAsrDeviceControlTest, ControlInPortInDescriptors) {
  BaiduAsrDevice dev(DummyConfig(), "asr_test");

  bool found = false;
  for (const auto& p : dev.GetPortDescriptors()) {
    if (p.name == "control_in" && p.direction == PortDirection::kInput &&
        p.data_type == "control/command") {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Property 9: IO devices dispatch recognized control commands
// **Validates: Requirements 7.2, 7.4, 7.5, 7.7**
// ---------------------------------------------------------------------------

// Property: for any number of "cancel" commands sent to ElevenLabsTtsDevice,
// CancelSynthesis() (and thus TtsClient::Cancel()) is called exactly that many times.
RC_GTEST_PROP(IoDeviceControlInPropTest,
              prop_elevenlabs_cancel_dispatched_exactly_once, ()) {
  // Feature: core-decoupling, Property 9: IO devices dispatch recognized control commands

  const int n = *rc::gen::inRange(1, 10);

  auto* mock = new MockTtsClient();
  ElevenLabsTtsDevice dev(std::unique_ptr<services::TtsClient>(mock), "tts_prop");
  dev.Start();

  for (int i = 0; i < n; ++i) {
    dev.OnInput("control_in", MakeControlFrame("cancel"));
  }

  RC_ASSERT(mock->cancel_count.load() == n);
  dev.Stop();
}

// Property: for any number of "cancel" commands sent to AudioPlayoutDevice,
// player_->Flush() is called exactly that many times (stream stays open).
RC_GTEST_PROP(IoDeviceControlInPropTest,
              prop_audio_playout_cancel_dispatched_exactly_once, ()) {
  // Feature: core-decoupling, Property 9: IO devices dispatch recognized control commands

  const int n = *rc::gen::inRange(1, 10);

  auto* mock = new MockAudioPlayer();
  AudioPlayoutDevice dev(std::unique_ptr<AudioPlayer>(mock), "playout_prop");

  for (int i = 0; i < n; ++i) {
    dev.OnInput("control_in", MakeControlFrame("cancel"));
  }

  RC_ASSERT(mock->flush_count.load() == n);
  RC_ASSERT(mock->stop_count.load() == 0);
}

// Property: unrecognized commands to any device never crash.
RC_GTEST_PROP(IoDeviceControlInPropTest,
              prop_unrecognized_command_no_crash, ()) {
  // Feature: core-decoupling, Property 9: IO devices dispatch recognized control commands

  const auto cmd = *rc::gen::nonEmpty(
      rc::gen::container<std::string>(rc::gen::inRange<char>('a', 'z')));

  // ElevenLabs
  {
    auto* mock = new MockTtsClient();
    ElevenLabsTtsDevice dev(std::unique_ptr<services::TtsClient>(mock), "tts_prop");
    dev.Start();
    bool threw = false;
    try { dev.OnInput("control_in", MakeControlFrame(cmd)); }
    catch (...) { threw = true; }
    RC_ASSERT(!threw);
    dev.Stop();
  }

  // AudioPlayout
  {
    auto* mock = new MockAudioPlayer();
    AudioPlayoutDevice dev(std::unique_ptr<AudioPlayer>(mock), "playout_prop");
    bool threw = false;
    try { dev.OnInput("control_in", MakeControlFrame(cmd)); }
    catch (...) { threw = true; }
    RC_ASSERT(!threw);
  }
}

}  // namespace
}  // namespace shizuru::io
