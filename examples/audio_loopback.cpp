#include <cstdio>
#include <cstdint>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>

#include "audio_device/audio_device.h"
#include "audio_device/port_audio/pa_recorder.h"
#include "audio_device/port_audio/pa_player.h"

int main() {
  try {
    constexpr double kSampleRate = 16000.0;
    constexpr size_t kChannels = 1;
    constexpr size_t kFramesPerBuffer = 480;

    // Sample format is set via config enum — backend handles the rest.
    auto format = shizuru::io::SampleFormat::kInt16;

    shizuru::io::RecorderConfig rec_cfg;
    rec_cfg.sample_rate = kSampleRate;
    rec_cfg.channels = kChannels;
    rec_cfg.frames_per_buffer = kFramesPerBuffer;
    rec_cfg.format = format;

    shizuru::io::PlayerConfig play_cfg;
    play_cfg.sample_rate = kSampleRate;
    play_cfg.channels = kChannels;
    play_cfg.frames_per_buffer = kFramesPerBuffer;
    play_cfg.format = format;

    shizuru::io::AudioDevice device(
        std::make_unique<shizuru::io::PaRecorder>(rec_cfg),
        std::make_unique<shizuru::io::PaPlayer>(play_cfg));

    std::printf("Format: %s\n",
                format == shizuru::io::SampleFormat::kFloat32 ? "float32" : "s16le");
    std::printf("Starting 5s loopback (mic -> speaker)...\n");
    device.StartRecording();
    device.StartPlayout();

    size_t bytes_per_sample = shizuru::io::BytesPerSample(format);
    std::vector<uint8_t> buf(kFramesPerBuffer * kChannels * bytes_per_sample);
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < end) {
      size_t read = device.Recorder().Read(buf.data(), kFramesPerBuffer);
      if (read > 0) {
        static auto *fp = fopen("a.pcm", "wbe");
        if (fp) {
          fwrite(buf.data(), 
                          1, read * bytes_per_sample, fp);
        }
        device.Player().Write(buf.data(), read);
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
