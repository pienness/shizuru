#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "interfaces/io_bridge.h"

namespace shizuru::core::testing {

// Hand-written mock for IoBridge.
// Set execute_fn to control Execute behavior in tests.
// All calls are recorded in execute_calls for verification.
class MockIoBridge : public IoBridge {
 public:
  // Configurable behavior for Execute calls.
  std::function<ActionResult(const ActionCandidate&)> execute_fn;

  // Recorded Execute calls (each is the ActionCandidate that was passed).
  std::vector<ActionCandidate> execute_calls;

  // Number of Cancel() calls received.
  int cancel_count = 0;

  ActionResult Execute(const ActionCandidate& action) override {
    std::lock_guard<std::mutex> lock(mu_);
    execute_calls.push_back(action);
    if (execute_fn) {
      return execute_fn(action);
    }
    // Default: return success with empty output
    return ActionResult{true, "", ""};
  }

  void Cancel() override {
    std::lock_guard<std::mutex> lock(mu_);
    ++cancel_count;
  }

 private:
  std::mutex mu_;
};

}  // namespace shizuru::core::testing
