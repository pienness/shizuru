#pragma once

#include <memory>
#include "audio_device/audio_recorder.h"
#include "audio_device/audio_player.h"

namespace shizuru::io {

// Concrete facade that owns a recorder and a player.
// Not abstract — abstraction lives at the recorder/player level.
class AudioDevice {
 public:
  AudioDevice(std::unique_ptr<AudioRecorder> recorder,
              std::unique_ptr<AudioPlayer> player)
      : recorder_(std::move(recorder)), player_(std::move(player)) {}

  ~AudioDevice() {
    StopRecording();
    StopPlayout();
  }

  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  void StartRecording() { recorder_->Start(); }
  void StopRecording()  { recorder_->Stop(); }
  bool IsRecording() const { return recorder_->IsRecording(); }

  void StartPlayout() { player_->Start(); }
  void StopPlayout()  { player_->Stop(); }
  bool IsPlaying() const { return player_->IsPlaying(); }

  AudioRecorder& Recorder() { return *recorder_; }
  AudioPlayer& Player() { return *player_; }

 private:
  std::unique_ptr<AudioRecorder> recorder_;
  std::unique_ptr<AudioPlayer> player_;
};

}  // namespace shizuru::io
