#pragma once

#include <string>
#include <vector>

#include "io/io_device.h"

namespace shizuru::io {

// VadEventDevice — forwards VAD events as vad/event DataFrames on vad_out.
//
// Port contract:
//   Input  "vad_in"  — vad/event DataFrames from EnergyVadDevice
//   Output "vad_out" — vad/event DataFrames (payload = raw event name bytes)
//
// Every event received on vad_in is re-emitted on vad_out unchanged.
// Downstream devices (e.g. CoreDevice) decide which events to act on.
class VadEventDevice : public IoDevice {
 public:
  explicit VadEventDevice(std::string device_id = "vad_event");

  std::string GetDeviceId() const override;
  std::vector<PortDescriptor> GetPortDescriptors() const override;
  void OnInput(const std::string& port_name, DataFrame frame) override;
  void SetOutputCallback(OutputCallback cb) override;
  void Start() override;
  void Stop() override;

  static constexpr char kVadIn[]  = "vad_in";
  static constexpr char kVadOut[] = "vad_out";

 private:
  std::string device_id_;
  OutputCallback output_cb_;
};

}  // namespace shizuru::io
