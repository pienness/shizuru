#pragma once

#include <string>

#include "controller/types.h"

namespace shizuru::core {

// Result of an IO action execution.
struct ActionResult {
  bool success = false;
  std::string output;        // Serialized result data
  std::string error_message; // Non-empty on failure
};

// Abstract interface for dispatching IO actions and receiving observations.
class IoBridge {
 public:
  virtual ~IoBridge() = default;

  // Execute an IO action synchronously. Returns the result.
  virtual ActionResult Execute(const ActionCandidate& action) = 0;

  // Request cancellation of an in-progress action.
  virtual void Cancel() = 0;
};

}  // namespace shizuru::core
