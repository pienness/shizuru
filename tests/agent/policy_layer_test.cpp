// Unit tests for PolicyLayer
// Tests specific examples, edge cases, and integration points.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "controller/types.h"
#include "interfaces/audit_sink.h"
#include "mock_audit_sink.h"
#include "policy/config.h"
#include "policy/policy_layer.h"
#include "policy/types.h"

namespace shizuru::core {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class PolicyLayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sink_ = std::make_unique<testing::MockAuditSink>();
  }

  PolicyLayer MakeLayer(PolicyConfig config) {
    return PolicyLayer(std::move(config), *sink_);
  }

  std::unique_ptr<testing::MockAuditSink> sink_;
  const std::string kSessionId = "unit-test-session";
};

ActionCandidate MakeAction(const std::string& name,
                           const std::string& capability = "") {
  ActionCandidate ac;
  ac.type = ActionType::kToolCall;
  ac.action_name = name;
  ac.arguments = "{}";
  ac.response_text = "";
  ac.required_capability = capability;
  return ac;
}

// ---------------------------------------------------------------------------
// Rule matching
// Requirements: 9.1, 9.2, 9.3
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, ExactRuleMatchAllow) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "read_file";
  rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result = layer.CheckPermission(kSessionId, MakeAction("read_file"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kAllow);
}

TEST_F(PolicyLayerTest, ExactRuleMatchDeny) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "delete_file";
  rule.outcome = PolicyOutcome::kDeny;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result = layer.CheckPermission(kSessionId, MakeAction("delete_file"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kDeny);
  EXPECT_FALSE(result.reason.empty());
}

TEST_F(PolicyLayerTest, HigherPriorityRuleWins) {
  PolicyRule deny_rule;
  deny_rule.priority = 10;
  deny_rule.action_pattern = "write_file";
  deny_rule.outcome = PolicyOutcome::kDeny;

  PolicyRule allow_rule;
  allow_rule.priority = 1;
  allow_rule.action_pattern = "write_file";
  allow_rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {deny_rule, allow_rule};  // inserted out of order
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result = layer.CheckPermission(kSessionId, MakeAction("write_file"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kAllow);
}

// ---------------------------------------------------------------------------
// Approval flow
// Requirements: 9.4, 9.5
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, ApprovalFlowRequestAndApprove) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "dangerous_action";
  rule.outcome = PolicyOutcome::kRequireApproval;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result =
      layer.CheckPermission(kSessionId, MakeAction("dangerous_action"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kRequireApproval);
  EXPECT_NE(result.request_id, 0u);

  // Approve — should not throw.
  EXPECT_NO_THROW(
      layer.ResolveApproval(kSessionId, result.request_id, true));
}

TEST_F(PolicyLayerTest, ApprovalFlowRequestAndDeny) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "dangerous_action";
  rule.outcome = PolicyOutcome::kRequireApproval;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result =
      layer.CheckPermission(kSessionId, MakeAction("dangerous_action"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kRequireApproval);
  EXPECT_NE(result.request_id, 0u);

  // Deny — should not throw.
  EXPECT_NO_THROW(
      layer.ResolveApproval(kSessionId, result.request_id, false));
}

TEST_F(PolicyLayerTest, ApprovalRequestIdsAreUnique) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "action";
  rule.outcome = PolicyOutcome::kRequireApproval;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto r1 = layer.CheckPermission(kSessionId, MakeAction("action"));
  auto r2 = layer.CheckPermission(kSessionId, MakeAction("action"));
  EXPECT_NE(r1.request_id, r2.request_id);
}

// ---------------------------------------------------------------------------
// Capability grant/revoke
// Requirements: 8.1, 8.2, 8.3, 8.4, 8.5
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, CapabilityGrantAndCheck) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  EXPECT_FALSE(layer.HasCapability(kSessionId, "file_read"));
  layer.GrantCapability(kSessionId, "file_read");
  EXPECT_TRUE(layer.HasCapability(kSessionId, "file_read"));
}

TEST_F(PolicyLayerTest, CapabilityRevokeAndCheck) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.GrantCapability(kSessionId, "file_read");
  EXPECT_TRUE(layer.HasCapability(kSessionId, "file_read"));

  layer.RevokeCapability(kSessionId, "file_read");
  EXPECT_FALSE(layer.HasCapability(kSessionId, "file_read"));
}

TEST_F(PolicyLayerTest, DefaultCapabilitiesFromConfig) {
  PolicyConfig config;
  config.default_capabilities = {"cap_a", "cap_b"};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap_a"));
  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap_b"));
  EXPECT_FALSE(layer.HasCapability(kSessionId, "cap_c"));
}

TEST_F(PolicyLayerTest, CapabilityRequiredByRuleAllowsWhenPresent) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "read_file";
  rule.required_capability = "file_read";
  rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  // Without capability → deny (rule doesn't match)
  auto result = layer.CheckPermission(kSessionId, MakeAction("read_file"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kDeny);

  // With capability → allow
  layer.GrantCapability(kSessionId, "file_read");
  result = layer.CheckPermission(kSessionId, MakeAction("read_file"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kAllow);
}

TEST_F(PolicyLayerTest, MultipleCapabilitiesIndependent) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.GrantCapability(kSessionId, "cap_a");
  layer.GrantCapability(kSessionId, "cap_b");

  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap_a"));
  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap_b"));

  layer.RevokeCapability(kSessionId, "cap_a");
  EXPECT_FALSE(layer.HasCapability(kSessionId, "cap_a"));
  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap_b"));
}

// ---------------------------------------------------------------------------
// Deny-by-default
// Requirements: 9.6
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, DenyByDefaultNoRules) {
  PolicyConfig config;  // No rules
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result = layer.CheckPermission(kSessionId, MakeAction("anything"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kDeny);
}

TEST_F(PolicyLayerTest, DenyByDefaultNoMatchingRule) {
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = "specific_action";
  rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {rule};
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto result =
      layer.CheckPermission(kSessionId, MakeAction("different_action"));
  EXPECT_EQ(result.outcome, PolicyOutcome::kDeny);
}

// ---------------------------------------------------------------------------
// Audit record verification
// Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, AuditTransitionRecordFields) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.AuditTransition(kSessionId, State::kIdle, State::kListening,
                        Event::kStart);

  ASSERT_EQ(sink_->records.size(), 1u);
  const auto& rec = sink_->records[0];

  EXPECT_EQ(rec.session_id, kSessionId);
  EXPECT_TRUE(rec.previous_state.has_value());
  EXPECT_EQ(rec.previous_state.value(), State::kIdle);
  EXPECT_TRUE(rec.new_state.has_value());
  EXPECT_EQ(rec.new_state.value(), State::kListening);
  EXPECT_TRUE(rec.triggering_event.has_value());
  EXPECT_EQ(rec.triggering_event.value(), Event::kStart);
  EXPECT_GT(rec.sequence_number, 0u);
  EXPECT_GT(rec.timestamp.time_since_epoch().count(), 0);
}

TEST_F(PolicyLayerTest, AuditActionRecordFieldsAllow) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);
  layer.GrantCapability(kSessionId, "test_cap");

  auto action = MakeAction("test_action");
  PolicyResult pr{PolicyOutcome::kAllow, "Allowed", 0};
  layer.AuditAction(kSessionId, action, pr);

  ASSERT_EQ(sink_->records.size(), 1u);
  const auto& rec = sink_->records[0];

  EXPECT_EQ(rec.session_id, kSessionId);
  EXPECT_TRUE(rec.action_type.has_value());
  EXPECT_EQ(rec.action_type.value(), "test_action");
  EXPECT_TRUE(rec.target_resource.has_value());
  EXPECT_TRUE(rec.policy_outcome.has_value());
  EXPECT_EQ(rec.policy_outcome.value(), PolicyOutcome::kAllow);
  EXPECT_FALSE(rec.granted_capabilities.empty());
  // Deny fields should not be set for Allow.
  EXPECT_FALSE(rec.denial_reason.has_value());
  EXPECT_FALSE(rec.matching_rule.has_value());
}

TEST_F(PolicyLayerTest, AuditActionRecordFieldsDeny) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  auto action = MakeAction("test_action");
  PolicyResult pr{PolicyOutcome::kDeny, "Denied by rule", 0};
  layer.AuditAction(kSessionId, action, pr);

  ASSERT_EQ(sink_->records.size(), 1u);
  const auto& rec = sink_->records[0];

  EXPECT_EQ(rec.policy_outcome.value(), PolicyOutcome::kDeny);
  EXPECT_TRUE(rec.denial_reason.has_value());
  EXPECT_FALSE(rec.denial_reason.value().empty());
  EXPECT_TRUE(rec.matching_rule.has_value());
  EXPECT_FALSE(rec.matching_rule.value().empty());
}

TEST_F(PolicyLayerTest, AuditSequenceNumbersMonotonicallyIncrease) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.AuditTransition(kSessionId, State::kIdle, State::kListening,
                        Event::kStart);
  layer.AuditTransition(kSessionId, State::kListening, State::kThinking,
                        Event::kUserObservation);

  auto action = MakeAction("test");
  PolicyResult pr{PolicyOutcome::kAllow, "ok", 0};
  layer.AuditAction(kSessionId, action, pr);

  ASSERT_EQ(sink_->records.size(), 3u);
  for (size_t i = 1; i < sink_->records.size(); ++i) {
    EXPECT_GT(sink_->records[i].sequence_number,
              sink_->records[i - 1].sequence_number);
  }
}

TEST_F(PolicyLayerTest, AuditSessionIdMatchesSession) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.AuditTransition(kSessionId, State::kIdle, State::kListening,
                        Event::kStart);

  ASSERT_EQ(sink_->records.size(), 1u);
  EXPECT_EQ(sink_->records[0].session_id, kSessionId);
}

// ---------------------------------------------------------------------------
// Session lifecycle
// Requirements: 8.1
// ---------------------------------------------------------------------------

TEST_F(PolicyLayerTest, ReleaseSessionClearsCapabilities) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  layer.GrantCapability(kSessionId, "cap");
  EXPECT_TRUE(layer.HasCapability(kSessionId, "cap"));

  layer.ReleaseSession(kSessionId);
  EXPECT_FALSE(layer.HasCapability(kSessionId, "cap"));
}

TEST_F(PolicyLayerTest, ResolveUnknownApprovalDoesNotCrash) {
  PolicyConfig config;
  auto layer = MakeLayer(config);
  layer.InitSession(kSessionId);

  // Resolving a non-existent request_id should be a no-op.
  EXPECT_NO_THROW(layer.ResolveApproval(kSessionId, 99999, true));
}

}  // namespace
}  // namespace shizuru::core
