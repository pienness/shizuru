// Unit and property-based tests for ToolDispatchDevice
// Uses Google Test + RapidCheck

// Feature: core-decoupling, Property 4: ToolDispatchDevice tool call round-trip

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "io/data_frame.h"
#include "runtime/tool_dispatch_device.h"
#include "services/io/tool_registry.h"

namespace shizuru::runtime {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build an action/tool_call DataFrame with payload "<name>:<args>".
io::DataFrame MakeToolCallFrame(const std::string& name,
                                const std::string& args) {
  const std::string payload = name + ":" + args;
  io::DataFrame frame;
  frame.type = "action/tool_call";
  frame.payload = std::vector<uint8_t>(payload.begin(), payload.end());
  frame.timestamp = std::chrono::steady_clock::now();
  return frame;
}

// Wait up to timeout_ms for predicate to become true, polling every 5ms.
bool WaitFor(std::function<bool()> predicate, int timeout_ms = 500) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

// ---------------------------------------------------------------------------
// Property 4: ToolDispatchDevice tool call round-trip
// **Validates: Requirements 3.3**
// ---------------------------------------------------------------------------
RC_GTEST_PROP(ToolDispatchDevicePropTest, prop_tool_call_round_trip, ()) {
  // For any registered tool name and args, dispatch produces a success result
  // frame containing the tool's return value.
  const std::string name = *rc::gen::nonEmpty(
      rc::gen::container<std::string>(
          rc::gen::inRange('a', static_cast<char>('z' + 1))));
  const std::string args = *rc::gen::container<std::string>(
      rc::gen::inRange('a', static_cast<char>('z' + 1)));
  const std::string return_value = *rc::gen::container<std::string>(
      rc::gen::inRange('a', static_cast<char>('z' + 1)));

  services::ToolRegistry registry;
  // Register a tool that echoes the return_value.
  registry.Register(name, [return_value](const std::string&) {
    services::ToolResult r;
    r.success = true;
    r.output = return_value;
    return r;
  });

  ToolDispatchDevice device(registry);

  std::mutex mu;
  std::vector<io::DataFrame> emitted;
  device.SetOutputCallback([&](const std::string& /*dev*/,
                               const std::string& /*port*/,
                               io::DataFrame f) {
    std::lock_guard<std::mutex> lock(mu);
    emitted.push_back(std::move(f));
  });

  device.Start();
  device.OnInput("action_in", MakeToolCallFrame(name, args));

  bool got_result = WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    return !emitted.empty();
  });

  device.Stop();

  RC_ASSERT(got_result);

  std::lock_guard<std::mutex> lock(mu);
  RC_ASSERT(!emitted.empty());
  const auto& f = emitted.front();
  RC_ASSERT(f.type == "action/tool_result");

  const std::string payload(f.payload.begin(), f.payload.end());
  RC_ASSERT(payload.find(R"("success":true)") != std::string::npos);
  // The return value must appear in the output field.
  if (!return_value.empty()) {
    RC_ASSERT(payload.find(return_value) != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// Unit Test: Successful dispatch
// ---------------------------------------------------------------------------
TEST(ToolDispatchDeviceTest, SuccessfulDispatch) {
  services::ToolRegistry registry;
  registry.Register("echo", [](const std::string& args) {
    services::ToolResult r;
    r.success = true;
    r.output = args;
    return r;
  });

  ToolDispatchDevice device(registry);

  std::mutex mu;
  std::vector<io::DataFrame> emitted;
  device.SetOutputCallback([&](const std::string&, const std::string&,
                               io::DataFrame f) {
    std::lock_guard<std::mutex> lock(mu);
    emitted.push_back(std::move(f));
  });

  device.Start();
  device.OnInput("action_in", MakeToolCallFrame("echo", "hello"));

  bool got = WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    return !emitted.empty();
  });

  device.Stop();

  ASSERT_TRUE(got);
  std::lock_guard<std::mutex> lock(mu);
  ASSERT_EQ(emitted.size(), 1u);
  const std::string payload(emitted[0].payload.begin(), emitted[0].payload.end());
  EXPECT_EQ(emitted[0].type, "action/tool_result");
  EXPECT_NE(payload.find(R"("success":true)"), std::string::npos);
  EXPECT_NE(payload.find("hello"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unit Test: Unknown tool name → failure result frame
// ---------------------------------------------------------------------------
TEST(ToolDispatchDeviceTest, UnknownToolEmitsFailureFrame) {
  services::ToolRegistry registry;  // empty — no tools registered

  ToolDispatchDevice device(registry);

  std::mutex mu;
  std::vector<io::DataFrame> emitted;
  device.SetOutputCallback([&](const std::string&, const std::string&,
                               io::DataFrame f) {
    std::lock_guard<std::mutex> lock(mu);
    emitted.push_back(std::move(f));
  });

  device.Start();
  device.OnInput("action_in", MakeToolCallFrame("no_such_tool", "{}"));

  bool got = WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    return !emitted.empty();
  });

  device.Stop();

  ASSERT_TRUE(got);
  std::lock_guard<std::mutex> lock(mu);
  ASSERT_EQ(emitted.size(), 1u);
  const std::string payload(emitted[0].payload.begin(), emitted[0].payload.end());
  EXPECT_EQ(emitted[0].type, "action/tool_result");
  EXPECT_NE(payload.find(R"("success":false)"), std::string::npos);
  EXPECT_NE(payload.find("Unknown tool: no_such_tool"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unit Test: Tool throws exception → failure frame, device continues
// ---------------------------------------------------------------------------
TEST(ToolDispatchDeviceTest, ThrowingToolEmitsFailureAndDeviceContinues) {
  services::ToolRegistry registry;
  registry.Register("boom", [](const std::string&) -> services::ToolResult {
    throw std::runtime_error("intentional error");
  });
  registry.Register("ok", [](const std::string&) {
    services::ToolResult r;
    r.success = true;
    r.output = "survived";
    return r;
  });

  ToolDispatchDevice device(registry);

  std::mutex mu;
  std::vector<io::DataFrame> emitted;
  device.SetOutputCallback([&](const std::string&, const std::string&,
                               io::DataFrame f) {
    std::lock_guard<std::mutex> lock(mu);
    emitted.push_back(std::move(f));
  });

  device.Start();
  device.OnInput("action_in", MakeToolCallFrame("boom", "{}"));
  device.OnInput("action_in", MakeToolCallFrame("ok", "{}"));

  bool got_two = WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    return emitted.size() >= 2;
  });

  device.Stop();

  ASSERT_TRUE(got_two) << "Expected 2 result frames, got "
                       << emitted.size();

  std::lock_guard<std::mutex> lock(mu);
  // First frame: failure from "boom"
  const std::string p0(emitted[0].payload.begin(), emitted[0].payload.end());
  EXPECT_NE(p0.find(R"("success":false)"), std::string::npos);
  EXPECT_NE(p0.find("intentional error"), std::string::npos);

  // Second frame: success from "ok"
  const std::string p1(emitted[1].payload.begin(), emitted[1].payload.end());
  EXPECT_NE(p1.find(R"("success":true)"), std::string::npos);
  EXPECT_NE(p1.find("survived"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unit Test: Stop() drains queue before joining
// ---------------------------------------------------------------------------
TEST(ToolDispatchDeviceTest, StopDrainsQueueBeforeJoining) {
  services::ToolRegistry registry;

  // Tool that takes a small but measurable amount of time.
  std::atomic<int> executed{0};
  registry.Register("slow", [&](const std::string&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ++executed;
    services::ToolResult r;
    r.success = true;
    r.output = "done";
    return r;
  });

  ToolDispatchDevice device(registry);

  std::mutex mu;
  std::vector<io::DataFrame> emitted;
  device.SetOutputCallback([&](const std::string&, const std::string&,
                               io::DataFrame f) {
    std::lock_guard<std::mutex> lock(mu);
    emitted.push_back(std::move(f));
  });

  device.Start();

  // Enqueue several tasks before calling Stop().
  constexpr int kCount = 3;
  for (int i = 0; i < kCount; ++i) {
    device.OnInput("action_in", MakeToolCallFrame("slow", "{}"));
  }

  // Stop() must block until all tasks are processed.
  device.Stop();

  // After Stop() returns, all tasks must have been executed and results emitted.
  EXPECT_EQ(executed.load(), kCount);
  std::lock_guard<std::mutex> lock(mu);
  EXPECT_EQ(static_cast<int>(emitted.size()), kCount);
}

}  // namespace
}  // namespace shizuru::runtime
