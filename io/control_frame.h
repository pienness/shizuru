#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "io/data_frame.h"

namespace shizuru::io {

struct ControlFrame {
  static constexpr char kCommandCancel[] = "cancel";
  static constexpr char kCommandFlush[]  = "flush";

  // Serialize a command into a DataFrame.
  static DataFrame Make(std::string_view command) {
    const std::string payload = R"({"command":")" +
                                std::string(command) + R"("})";
    DataFrame frame;
    frame.type    = "control/command";
    frame.payload = std::vector<uint8_t>(payload.begin(), payload.end());
    frame.timestamp = std::chrono::steady_clock::now();
    return frame;
  }

  // Parse a "control/command" DataFrame. Returns empty string on failure.
  static std::string Parse(const DataFrame& frame) {
    if (frame.type != "control/command") { return {}; }
    const std::string json(frame.payload.begin(), frame.payload.end());
    // Minimal parse: find "command":"<value>"
    const auto key_pos = json.find(R"("command":")");
    if (key_pos == std::string::npos) { return {}; }
    const auto val_start = key_pos + 11;  // len of "command":"
    const auto val_end   = json.find('"', val_start);
    if (val_end == std::string::npos) { return {}; }
    return json.substr(val_start, val_end - val_start);
  }
};

}  // namespace shizuru::io
