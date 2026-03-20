#pragma once

#include <functional>
#include <string>
#include <vector>

#include "io/data_frame.h"

namespace shizuru::io {

enum class PortDirection { kInput, kOutput };

struct PortDescriptor {
  std::string name;       // e.g., "audio_in", "text_out"
  PortDirection direction;
  std::string data_type;  // MIME-like: "audio/pcm", "text/plain", etc.
};

using OutputCallback = std::function<void(
    const std::string& device_id,
    const std::string& port_name,
    DataFrame frame)>;

class IoDevice {
 public:
  virtual ~IoDevice() = default;

  // Unique identifier for this device instance.
  virtual std::string GetDeviceId() const = 0;

  // Ports this device exposes.
  virtual std::vector<PortDescriptor> GetPortDescriptors() const = 0;

  // Accept an incoming data frame on a named input port.
  virtual void OnInput(const std::string& port_name, DataFrame frame) = 0;

  // Register the callback the device uses to emit output frames.
  virtual void SetOutputCallback(OutputCallback cb) = 0;

  // Lifecycle.
  virtual void Start() = 0;
  virtual void Stop() = 0;
};

}  // namespace shizuru::io
