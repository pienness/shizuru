#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "io/io_device.h"

namespace shizuru::io {

// VadEventDevice — fires a callback when a specific VAD event is received.
//
// Port contract:
//   Input  "vad_in" — vad/event DataFrames from EnergyVadDevice
//   (no output ports)
//
// The trigger_events list controls which event strings fire the callback.
// Defaults to {"speech_end"}.
//
// Example uses:
//   - Trigger ASR flush on speech_end
//   - Start a recording indicator on speech_start
//   - Any other side-effect that should be driven by VAD state changes
class VadEventDevice : public IoDevice {
 public:
  using EventCallback = std::function<void(const std::string& event)>;

  explicit VadEventDevice(
      EventCallback on_event,
      std::vector<std::string> trigger_events = {"speech_end"},
      std::string device_id = "vad_event");

  std::string GetDeviceId() const override;
  std::vector<PortDescriptor> GetPortDescriptors() const override;
  void OnInput(const std::string& port_name, DataFrame frame) override;
  void SetOutputCallback(OutputCallback cb) override;
  void Start() override;
  void Stop() override;

  static constexpr char kVadIn[] = "vad_in";

 private:
  std::string device_id_;
  EventCallback on_event_;
  std::vector<std::string> trigger_events_;
};

}  // namespace shizuru::io
