#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace shizuru::io {

struct DataFrame {
  std::string type;       // MIME-like: "audio/pcm", "text/plain",
                          //            "text/json", "action/tool_call"
  std::vector<uint8_t> payload;  // Raw data bytes

  std::string source_device;     // Originating device ID
  std::string source_port;       // Originating port name

  std::chrono::steady_clock::time_point timestamp;

  // Optional key-value metadata.
  // Examples: {"sample_rate": "16000"}, {"language": "en"}, {"tool_name": "web_search"}
  std::unordered_map<std::string, std::string> metadata;
};

}  // namespace shizuru::io
