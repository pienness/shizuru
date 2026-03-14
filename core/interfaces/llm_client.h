#pragma once

#include <functional>
#include <string>

#include "context/types.h"
#include "controller/types.h"

namespace shizuru::core {

// Callback for streaming tokens from the LLM.
using StreamCallback = std::function<void(const std::string& token)>;

// Result of an LLM inference call.
struct LlmResult {
  ActionCandidate candidate;
  int prompt_tokens = 0;
  int completion_tokens = 0;
};

// Abstract interface for LLM service clients.
// Concrete implementations (OpenAI, Anthropic, local, etc.) live outside core/.
class LlmClient {
 public:
  virtual ~LlmClient() = default;

  // Submit a context window and receive an action candidate.
  // Blocks until the full response is available.
  virtual LlmResult Submit(const ContextWindow& context) = 0;

  // Submit with streaming callback for incremental token delivery.
  virtual LlmResult SubmitStreaming(const ContextWindow& context,
                                    StreamCallback on_token) = 0;

  // Request cancellation of an in-progress call.
  virtual void Cancel() = 0;
};

}  // namespace shizuru::core
