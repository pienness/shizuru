#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "audio_device/audio_device.h"
#include "audio_device/audio_frame.h"
#include "audio_device/port_audio/pa_player.h"
#include "audio_device/port_audio/pa_recorder.h"

int main() {
  try {
    constexpr int    kSampleRate       = 16000;
    constexpr size_t kChannels         = 1;
    constexpr size_t kFramesPerBuffer  = 320;  // 20ms at 16kHz

    shizuru::io::RecorderConfig rec_cfg;
    rec_cfg.sample_rate       = kSampleRate;
    rec_cfg.channel_count     = kChannels;
    rec_cfg.frames_per_buffer = kFramesPerBuffer;

    shizuru::io::PlayerConfig play_cfg;
    play_cfg.sample_rate       = kSampleRate;
    play_cfg.channel_count     = kChannels;
    play_cfg.frames_per_buffer = kFramesPerBuffer;

    shizuru::io::AudioDevice device(
        std::make_unique<shizuru::io::PaRecorder>(rec_cfg),
        std::make_unique<shizuru::io::PaPlayer>(play_cfg));

    std::printf("Format: s16le, %d Hz, %zu ch\n", kSampleRate, kChannels);
    std::printf("Starting 5s loopback (mic -> speaker)...\n");
    device.StartRecording();
    device.StartPlayout();

    shizuru::io::AudioFrame frame;
    frame.sample_rate   = kSampleRate;
    frame.channel_count = kChannels;
    frame.sample_count  = kFramesPerBuffer;

    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
      const size_t read = device.Recorder().Read(frame);
      if (read > 0) {
        frame.sample_count = read;
        device.Player().Write(frame);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    device.StopRecording();
    device.StopPlayout();
    std::printf("Done.\n");

  } catch (const std::exception& e) {
    std::fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
  return 0;
}
