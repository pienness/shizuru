#include "pcm_dump_device.h"

#include <utility>

namespace shizuru::io {

PcmDumpDevice::PcmDumpDevice(std::string name) : name_(std::move(name)) {}

PcmDumpDevice::~PcmDumpDevice() { Stop(); }

std::string PcmDumpDevice::GetDeviceId() const { return name_; }

std::vector<PortDescriptor> PcmDumpDevice::GetPortDescriptors() const {
  return {
      {kPassIn,  PortDirection::kInput,  "audio/pcm"},
      {kPassOut, PortDirection::kOutput, "audio/pcm"},
  };
}

void PcmDumpDevice::SetOutputCallback(OutputCallback cb) {
  output_cb_ = std::move(cb);
}

void PcmDumpDevice::Start() {
  const std::string path = name_ + ".pcm";
  file_.open(path, std::ios::binary | std::ios::trunc);
}

void PcmDumpDevice::Stop() {
  if (file_.is_open()) { file_.close(); }
}

void PcmDumpDevice::OnInput(const std::string& port_name, DataFrame frame) {
  if (port_name != kPassIn) { return; }
  if (frame.type != "audio/pcm") { return; }

  if (file_.is_open() && !frame.payload.empty()) {
    file_.write(reinterpret_cast<const char*>(frame.payload.data()),
                static_cast<std::streamsize>(frame.payload.size()));
  }

  if (output_cb_) {
    output_cb_(name_, kPassOut, std::move(frame));
  }
}

}  // namespace shizuru::io
