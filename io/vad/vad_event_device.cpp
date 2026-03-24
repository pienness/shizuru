#include "vad_event_device.h"

#include <chrono>
#include <utility>

#include "async_logger.h"

namespace shizuru::io {

VadEventDevice::VadEventDevice(std::string device_id)
    : device_id_(std::move(device_id)) {}

std::string VadEventDevice::GetDeviceId() const { return device_id_; }

std::vector<PortDescriptor> VadEventDevice::GetPortDescriptors() const {
  return {
      {kVadIn,  PortDirection::kInput,  "vad/event"},
      {kVadOut, PortDirection::kOutput, "vad/event"},
  };
}

void VadEventDevice::OnInput(const std::string& port_name, DataFrame frame) {
  if (port_name != kVadIn) { return; }

  const std::string event(frame.payload.begin(), frame.payload.end());
  LOG_INFO("VadEventDevice: received event '{}'", event);

  if (!output_cb_) {
    LOG_WARN("VadEventDevice: no output_cb, dropping event '{}'", event);
    return;
  }

  // Re-emit the event payload on vad_out.
  DataFrame out;
  out.type           = "vad/event";
  out.payload        = frame.payload;
  out.source_device  = device_id_;
  out.source_port    = kVadOut;
  out.timestamp      = std::chrono::steady_clock::now();

  output_cb_(device_id_, kVadOut, std::move(out));
  LOG_DEBUG("VadEventDevice: emitted '{}' on vad_out", event);
}

void VadEventDevice::SetOutputCallback(OutputCallback cb) {
  output_cb_ = std::move(cb);
}

void VadEventDevice::Start() {}
void VadEventDevice::Stop() {}

}  // namespace shizuru::io
