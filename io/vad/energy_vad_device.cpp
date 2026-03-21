#include "energy_vad_device.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace shizuru::io {

EnergyVadDevice::EnergyVadDevice(EnergyVadConfig config, std::string device_id)
    : device_id_(std::move(device_id)), config_(config) {}

std::string EnergyVadDevice::GetDeviceId() const { return device_id_; }

std::vector<PortDescriptor> EnergyVadDevice::GetPortDescriptors() const {
  return {
      {kAudioIn,  PortDirection::kInput,  "audio/pcm"},
      {kAudioOut, PortDirection::kOutput, "audio/pcm"},
      {kVadOut,   PortDirection::kOutput, "vad/event"},
  };
}

void EnergyVadDevice::SetOutputCallback(OutputCallback cb) {
  std::lock_guard<std::mutex> lock(output_cb_mutex_);
  output_cb_ = std::move(cb);
}

void EnergyVadDevice::Start() {
  in_speech_        = false;
  onset_counter_    = 0;
  hangover_counter_ = 0;
  rms_window_.clear();
  pre_roll_buf_.clear();
  active_.store(true);
}

void EnergyVadDevice::Stop() { active_.store(false); }

void EnergyVadDevice::OnInput(const std::string& port_name, DataFrame frame) {
  if (!active_.load()) { return; }
  if (port_name != kAudioIn) { return; }
  if (frame.payload.empty()) { return; }

  const float rms = ComputeRms(frame.payload);

  // Sliding window max-filter.
  rms_window_.push_back(rms);
  if (rms_window_.size() > config_.rms_window_frames) {
    rms_window_.pop_front();
  }
  const float window_max =
      *std::max_element(rms_window_.begin(), rms_window_.end());
  const bool is_speech = (window_max >= config_.energy_threshold);

  if (!in_speech_) {
    // Always maintain the pre-roll buffer while not in speech.
    pre_roll_buf_.push_back(frame);
    if (pre_roll_buf_.size() > config_.pre_roll_frames) {
      pre_roll_buf_.pop_front();
    }

    if (is_speech) {
      ++onset_counter_;
      hangover_counter_ = 0;
      if (onset_counter_ >= config_.speech_onset_frames) {
        in_speech_     = true;
        onset_counter_ = 0;
        EmitEvent("speech_start");
        // Pre-roll buffer contains up to pre_roll_frames recent frames.
        // If pre_roll_frames >= speech_onset_frames, the onset frames are
        // already in the buffer and FlushPreRoll replays them all.
        // If pre_roll_frames == 0, the buffer is empty; emit the current
        // frame directly so it is never lost.
        if (config_.pre_roll_frames == 0) {
          EmitAudio(std::move(frame));
        } else {
          FlushPreRoll();
        }
      }
    } else {
      onset_counter_ = 0;
    }
  } else {
    // In speech: forward every frame.
    if (is_speech) {
      hangover_counter_ = 0;
      EmitAudio(std::move(frame));
      EmitEvent("speech_active");
    } else {
      ++hangover_counter_;
      EmitAudio(std::move(frame));  // forward during hangover too
      if (hangover_counter_ >= config_.silence_hangover_frames) {
        in_speech_        = false;
        hangover_counter_ = 0;
        pre_roll_buf_.clear();
        EmitEvent("speech_end");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

float EnergyVadDevice::ComputeRms(const std::vector<uint8_t>& payload) {
  const size_t num_samples = payload.size() / sizeof(int16_t);
  if (num_samples == 0) { return 0.0F; }

  double sum_sq = 0.0;
  const auto* samples = reinterpret_cast<const int16_t*>(payload.data());
  for (size_t i = 0; i < num_samples; ++i) {
    const double s = static_cast<double>(samples[i]);
    sum_sq += s * s;
  }
  return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(num_samples)));  // NOLINT
}

void EnergyVadDevice::FlushPreRoll() {
  for (auto& f : pre_roll_buf_) {
    f.source_device = device_id_;
    f.source_port   = kAudioOut;
    OutputCallback cb;
    {
      std::lock_guard<std::mutex> lock(output_cb_mutex_);
      cb = output_cb_;
    }
    if (cb) { cb(device_id_, kAudioOut, f); }
  }
  pre_roll_buf_.clear();
}

void EnergyVadDevice::EmitAudio(DataFrame frame) {
  frame.source_device = device_id_;
  frame.source_port   = kAudioOut;
  OutputCallback cb;
  {
    std::lock_guard<std::mutex> lock(output_cb_mutex_);
    cb = output_cb_;
  }
  if (cb) { cb(device_id_, kAudioOut, std::move(frame)); }
}

void EnergyVadDevice::EmitEvent(const std::string& event) {
  const std::string json = R"({"event":")" + event + R"("})";

  DataFrame frame;
  frame.type    = "vad/event";
  frame.payload.assign(json.begin(), json.end());
  frame.source_device = device_id_;
  frame.source_port   = kVadOut;
  frame.timestamp     = std::chrono::steady_clock::now();

  OutputCallback cb;
  {
    std::lock_guard<std::mutex> lock(output_cb_mutex_);
    cb = output_cb_;
  }
  if (cb) { cb(device_id_, kVadOut, std::move(frame)); }
}

}  // namespace shizuru::io
