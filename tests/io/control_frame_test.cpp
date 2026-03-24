// Unit + property-based tests for ControlFrame
// Feature: core-decoupling, Property 3: ControlFrame round-trip

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <string>
#include <vector>

#include "io/control_frame.h"

namespace shizuru::io {
namespace {

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

TEST(ControlFrameTest, MakeProducesControlCommandType) {
  const auto frame = ControlFrame::Make("cancel");
  EXPECT_EQ(frame.type, "control/command");
}

TEST(ControlFrameTest, MakeCancelPayloadContainsCancel) {
  const auto frame = ControlFrame::Make(ControlFrame::kCommandCancel);
  const std::string payload(frame.payload.begin(), frame.payload.end());
  EXPECT_NE(payload.find("cancel"), std::string::npos);
}

TEST(ControlFrameTest, MakeFlushPayloadContainsFlush) {
  const auto frame = ControlFrame::Make(ControlFrame::kCommandFlush);
  const std::string payload(frame.payload.begin(), frame.payload.end());
  EXPECT_NE(payload.find("flush"), std::string::npos);
}

TEST(ControlFrameTest, ParseCancelReturnsCancel) {
  const auto frame = ControlFrame::Make("cancel");
  EXPECT_EQ(ControlFrame::Parse(frame), "cancel");
}

TEST(ControlFrameTest, ParseFlushReturnsFlush) {
  const auto frame = ControlFrame::Make("flush");
  EXPECT_EQ(ControlFrame::Parse(frame), "flush");
}

TEST(ControlFrameTest, ParseWrongTypeReturnsEmpty) {
  DataFrame frame;
  frame.type = "text/plain";
  frame.payload = {'c', 'a', 'n', 'c', 'e', 'l'};
  EXPECT_EQ(ControlFrame::Parse(frame), "");
}

TEST(ControlFrameTest, ParseMalformedPayloadReturnsEmpty) {
  DataFrame frame;
  frame.type = "control/command";
  const std::string bad = R"({"cmd":"cancel"})";  // wrong key
  frame.payload = std::vector<uint8_t>(bad.begin(), bad.end());
  EXPECT_EQ(ControlFrame::Parse(frame), "");
}

TEST(ControlFrameTest, ParseEmptyPayloadReturnsEmpty) {
  DataFrame frame;
  frame.type = "control/command";
  EXPECT_EQ(ControlFrame::Parse(frame), "");
}

TEST(ControlFrameTest, ParseMissingClosingQuoteReturnsEmpty) {
  DataFrame frame;
  frame.type = "control/command";
  const std::string bad = R"({"command":"cancel)";  // no closing quote
  frame.payload = std::vector<uint8_t>(bad.begin(), bad.end());
  EXPECT_EQ(ControlFrame::Parse(frame), "");
}

TEST(ControlFrameTest, TimestampIsSet) {
  const auto before = std::chrono::steady_clock::now();
  const auto frame = ControlFrame::Make("cancel");
  const auto after = std::chrono::steady_clock::now();
  EXPECT_GE(frame.timestamp, before);
  EXPECT_LE(frame.timestamp, after);
}

// ---------------------------------------------------------------------------
// Property 3: ControlFrame round-trip
// Validates: Requirements 5.3, 5.4
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ControlFramePropTest, prop_round_trip, (void)) {
  // Feature: core-decoupling, Property 3: ControlFrame round-trip
  const auto cmd = *rc::gen::container<std::string>(
      rc::gen::inRange<char>('a', 'z'));
  RC_ASSERT(ControlFrame::Parse(ControlFrame::Make(cmd)) == cmd);
}

RC_GTEST_PROP(ControlFramePropTest, prop_type_is_always_control_command, (void)) {
  const auto cmd = *rc::gen::container<std::string>(
      rc::gen::inRange<char>('a', 'z'));
  const auto frame = ControlFrame::Make(cmd);
  RC_ASSERT(frame.type == "control/command");
}

RC_GTEST_PROP(ControlFramePropTest, prop_wrong_type_always_returns_empty, (void)) {
  // Any type other than "control/command" must parse to empty.
  const auto type_chars = *rc::gen::container<std::string>(
      rc::gen::inRange<char>('a', 'z'));
  const std::string type = "text/" + type_chars;  // guaranteed != "control/command"

  DataFrame frame;
  frame.type = type;
  const std::string payload = R"({"command":"cancel"})";
  frame.payload = std::vector<uint8_t>(payload.begin(), payload.end());

  RC_ASSERT(ControlFrame::Parse(frame).empty());
}

}  // namespace
}  // namespace shizuru::io
