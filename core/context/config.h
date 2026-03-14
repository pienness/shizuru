#pragma once

#include <string>

namespace shizuru::core {

struct ContextConfig {
  int max_context_tokens = 8000;          // Token budget per context window
  int summarization_threshold = 50;       // Summarize when entries exceed this
  std::string default_system_instruction = "You are a helpful assistant.";
};

}  // namespace shizuru::core
