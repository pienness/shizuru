#include "vad_event_device.h"

#include <string_view>
#include <utility>

namespace shizuru::io {

VadEventDevice::VadEventDevice(EventCallback on_event,
                               std::vector<std::string> trigger_events,
                               std::string device_id)
    : device_id_(std::move(device_id)),
      on_event_(std::move(on_event)),
      trigger_events_(std::move(trigger_events)) {}

std::string VadEventDevice::GetDeviceId() const { return device_id_; }

std::vector<PortDescriptor> VadEventDevice::GetPortDescriptors() const {
  return {{kVadIn, PortDirection::kInput, "vad/event"}};
}

void VadEventDevice::OnInput(const std::string& port_name, DataFrame frame) {
  if (port_name != kVadIn) { return; }

  const std::string_view json(
      reinterpret_cast<const char*>(frame.payload.data()),
      frame.payload.size());

  for (const auto& event : trigger_events_) {
    if (json.find(event) != std::string_view::npos) {
      if (on_event_) { on_event_(event); }
      return;
    }
  }
}

void VadEventDevice::SetOutputCallback(OutputCallback /*cb*/) {}
void VadEventDevice::Start() {}
void VadEventDevice::Stop() {}

}  // namespace shizuru::io
