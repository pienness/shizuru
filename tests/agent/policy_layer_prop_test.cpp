// Property-based tests for PolicyLayer
// Uses RapidCheck + Google Test

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <set>
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
// RapidCheck generators
// ---------------------------------------------------------------------------

rc::Gen<std::string> genNonEmptyString() {
  return rc::gen::nonEmpty(
      rc::gen::container<std::string>(rc::gen::inRange('a', 'z')));
}

rc::Gen<ActionCandidate> genActionCandidate() {
  return rc::gen::build<ActionCandidate>(
      rc::gen::set(&ActionCandidate::type,
                   rc::gen::element(ActionType::kToolCall, ActionType::kResponse,
                                   ActionType::kContinue)),
      rc::gen::set(&ActionCandidate::action_name, genNonEmptyString()),
      rc::gen::set(&ActionCandidate::arguments, genNonEmptyString()),
      rc::gen::set(&ActionCandidate::response_text, genNonEmptyString()),
      rc::gen::set(&ActionCandidate::required_capability, genNonEmptyString()));
}

rc::Gen<PolicyOutcome> genPolicyOutcome() {
  return rc::gen::element(PolicyOutcome::kAllow, PolicyOutcome::kDeny,
                          PolicyOutcome::kRequireApproval);
}

rc::Gen<PolicyRule> genPolicyRule(const std::string& action_name) {
  return rc::gen::build<PolicyRule>(
      rc::gen::set(&PolicyRule::priority, rc::gen::inRange(0, 100)),
      rc::gen::set(&PolicyRule::action_pattern, rc::gen::just(action_name)),
      rc::gen::set(&PolicyRule::resource_pattern, genNonEmptyString()),
      rc::gen::set(&PolicyRule::required_capability, rc::gen::just(std::string(""))),
      rc::gen::set(&PolicyRule::outcome, genPolicyOutcome()));
}

rc::Gen<State> genState() {
  return rc::gen::element(State::kIdle, State::kListening, State::kThinking,
                          State::kRouting, State::kActing, State::kResponding,
                          State::kError, State::kTerminated);
}

rc::Gen<Event> genEvent() {
  return rc::gen::element(
      Event::kStart, Event::kStop, Event::kShutdown, Event::kUserObservation,
      Event::kLlmResult, Event::kLlmFailure, Event::kRouteToAction,
      Event::kRouteToResponse, Event::kRouteToContinue, Event::kActionComplete,
      Event::kActionFailed, Event::kResponseDelivered,
      Event::kStopConditionMet, Event::kInterrupt, Event::kRecover);
}

// ---------------------------------------------------------------------------
// Property 18: Capability-based permission check
// Feature: agent-core, Property 18: Capability-based permission check
// ---------------------------------------------------------------------------

// **Validates: Requirements 8.2, 8.3, 8.5**
RC_GTEST_PROP(PolicyLayerPropTest, prop_capability_permission_check,
              (void)) {
  // Generate a capability name and action candidate that requires it.
  auto capability = *genNonEmptyString();
  auto action = *genActionCandidate();
  action.required_capability = capability;

  // Create a rule that matches this action and requires the capability.
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = action.action_name;
  rule.required_capability = capability;
  rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {rule};

  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  // Case 1: Capability IS granted → Allow
  layer.GrantCapability(session_id, capability);
  auto result_allow = layer.CheckPermission(session_id, action);
  RC_ASSERT(result_allow.outcome == PolicyOutcome::kAllow);

  // Case 2: Capability is revoked → Deny (deny-by-default, rule won't match
  // without capability)
  layer.RevokeCapability(session_id, capability);
  auto result_deny = layer.CheckPermission(session_id, action);
  RC_ASSERT(result_deny.outcome == PolicyOutcome::kDeny);
  RC_ASSERT(!result_deny.reason.empty());
}

// ---------------------------------------------------------------------------
// Property 19: Capability grant-revoke round trip
// Feature: agent-core, Property 19: Capability grant-revoke round trip
// ---------------------------------------------------------------------------
// **Validates: Requirements 8.4**
RC_GTEST_PROP(PolicyLayerPropTest, prop_capability_grant_revoke, (void)) {
  auto capability = *genNonEmptyString();

  PolicyConfig config;
  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  // Initially not present
  RC_ASSERT(!layer.HasCapability(session_id, capability));

  // Grant → present
  layer.GrantCapability(session_id, capability);
  RC_ASSERT(layer.HasCapability(session_id, capability));

  // Revoke → not present
  layer.RevokeCapability(session_id, capability);
  RC_ASSERT(!layer.HasCapability(session_id, capability));
}

// ---------------------------------------------------------------------------
// Property 20: Policy rule priority ordering
// Feature: agent-core, Property 20: Policy rule priority ordering
// ---------------------------------------------------------------------------
// **Validates: Requirements 9.1, 9.2**
RC_GTEST_PROP(PolicyLayerPropTest, prop_rule_priority_ordering, (void)) {
  auto action_name = *genNonEmptyString();

  // Generate two distinct outcomes for the two rules.
  auto outcome_high = *rc::gen::element(PolicyOutcome::kAllow,
                                        PolicyOutcome::kDeny);
  auto outcome_low = (outcome_high == PolicyOutcome::kAllow)
                         ? PolicyOutcome::kDeny
                         : PolicyOutcome::kAllow;

  // High-priority rule (lower number).
  PolicyRule high_rule;
  high_rule.priority = 1;
  high_rule.action_pattern = action_name;
  high_rule.required_capability = "";
  high_rule.outcome = outcome_high;

  // Low-priority rule (higher number).
  PolicyRule low_rule;
  low_rule.priority = 10;
  low_rule.action_pattern = action_name;
  low_rule.required_capability = "";
  low_rule.outcome = outcome_low;

  // Insert in reverse order to ensure sorting matters.
  PolicyConfig config;
  config.initial_rules = {low_rule, high_rule};

  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  ActionCandidate action;
  action.type = ActionType::kToolCall;
  action.action_name = action_name;

  auto result = layer.CheckPermission(session_id, action);
  RC_ASSERT(result.outcome == outcome_high);
}

// ---------------------------------------------------------------------------
// Property 21: RequireApproval suspends and resolves
// Feature: agent-core, Property 21: RequireApproval suspends and resolves
// ---------------------------------------------------------------------------
// **Validates: Requirements 9.4, 9.5**
RC_GTEST_PROP(PolicyLayerPropTest, prop_require_approval_flow, (void)) {
  auto action_name = *genNonEmptyString();

  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = action_name;
  rule.required_capability = "";
  rule.outcome = PolicyOutcome::kRequireApproval;

  PolicyConfig config;
  config.initial_rules = {rule};

  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  ActionCandidate action;
  action.type = ActionType::kToolCall;
  action.action_name = action_name;

  auto result = layer.CheckPermission(session_id, action);
  RC_ASSERT(result.outcome == PolicyOutcome::kRequireApproval);
  RC_ASSERT(result.request_id != 0);

  // Resolve with approved=true — should not throw.
  layer.ResolveApproval(session_id, result.request_id, true);

  // Get a new approval request and resolve with approved=false.
  auto result2 = layer.CheckPermission(session_id, action);
  RC_ASSERT(result2.outcome == PolicyOutcome::kRequireApproval);
  RC_ASSERT(result2.request_id != 0);
  RC_ASSERT(result2.request_id != result.request_id);

  layer.ResolveApproval(session_id, result2.request_id, false);
}

// ---------------------------------------------------------------------------
// Property 22: No matching rule defaults to Deny
// Feature: agent-core, Property 22: No matching rule defaults to Deny
// ---------------------------------------------------------------------------
// **Validates: Requirements 9.6**
RC_GTEST_PROP(PolicyLayerPropTest, prop_no_match_defaults_deny, (void)) {
  auto action_name = *genNonEmptyString();

  // Create a rule that matches a DIFFERENT action name.
  PolicyRule rule;
  rule.priority = 0;
  rule.action_pattern = action_name + "_other";
  rule.required_capability = "";
  rule.outcome = PolicyOutcome::kAllow;

  PolicyConfig config;
  config.initial_rules = {rule};

  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  ActionCandidate action;
  action.type = ActionType::kToolCall;
  action.action_name = action_name;

  auto result = layer.CheckPermission(session_id, action);
  RC_ASSERT(result.outcome == PolicyOutcome::kDeny);
}

// ---------------------------------------------------------------------------
// Property 23: Audit record invariants
// Feature: agent-core, Property 23: Audit record invariants
// ---------------------------------------------------------------------------
// **Validates: Requirements 10.5, 10.6**
RC_GTEST_PROP(PolicyLayerPropTest, prop_audit_record_invariants, (void)) {
  PolicyConfig config;
  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = *genNonEmptyString();
  layer.InitSession(session_id);

  // Generate a random number of audit events (transitions + actions).
  int num_events = *rc::gen::inRange(2, 20);

  for (int i = 0; i < num_events; ++i) {
    if (*rc::gen::inRange(0, 2) == 0) {
      // Audit a transition
      auto from = *genState();
      auto to = *genState();
      auto event = *genEvent();
      layer.AuditTransition(session_id, from, to, event);
    } else {
      // Audit an action
      auto action = *genActionCandidate();
      PolicyResult pr{PolicyOutcome::kAllow, "test", 0};
      layer.AuditAction(session_id, action, pr);
    }
  }

  // Verify invariants on all records.
  RC_ASSERT(sink.records.size() == static_cast<size_t>(num_events));

  for (const auto& record : sink.records) {
    RC_ASSERT(!record.session_id.empty());
    RC_ASSERT(record.session_id == session_id);
  }

  // Sequence numbers must be strictly monotonically increasing.
  for (size_t i = 1; i < sink.records.size(); ++i) {
    RC_ASSERT(sink.records[i].sequence_number >
              sink.records[i - 1].sequence_number);
  }
}

// ---------------------------------------------------------------------------
// Property 24: Transition audit record completeness
// Feature: agent-core, Property 24: Transition audit record completeness
// ---------------------------------------------------------------------------
// **Validates: Requirements 10.1**
RC_GTEST_PROP(PolicyLayerPropTest, prop_transition_audit_completeness,
              (void)) {
  PolicyConfig config;
  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);

  auto from = *genState();
  auto to = *genState();
  auto event = *genEvent();

  layer.AuditTransition(session_id, from, to, event);

  RC_ASSERT(sink.records.size() == 1);
  const auto& record = sink.records[0];

  // Must have non-empty previous_state, new_state, triggering_event.
  RC_ASSERT(record.previous_state.has_value());
  RC_ASSERT(record.new_state.has_value());
  RC_ASSERT(record.triggering_event.has_value());

  RC_ASSERT(record.previous_state.value() == from);
  RC_ASSERT(record.new_state.value() == to);
  RC_ASSERT(record.triggering_event.value() == event);

  // Valid timestamp (non-default).
  RC_ASSERT(record.timestamp.time_since_epoch().count() > 0);
}

// ---------------------------------------------------------------------------
// Property 25: Action audit record completeness
// Feature: agent-core, Property 25: Action audit record completeness
// ---------------------------------------------------------------------------
// **Validates: Requirements 10.2, 10.3**
RC_GTEST_PROP(PolicyLayerPropTest, prop_action_audit_completeness, (void)) {
  auto capability = *genNonEmptyString();
  auto action = *genActionCandidate();

  PolicyConfig config;
  testing::MockAuditSink sink;
  PolicyLayer layer(config, sink);

  std::string session_id = "test-session";
  layer.InitSession(session_id);
  layer.GrantCapability(session_id, capability);

  // Test with Allow outcome.
  PolicyResult allow_result{PolicyOutcome::kAllow, "Allowed", 0};
  layer.AuditAction(session_id, action, allow_result);

  RC_ASSERT(sink.records.size() == 1);
  const auto& allow_record = sink.records[0];
  RC_ASSERT(allow_record.action_type.has_value());
  RC_ASSERT(allow_record.target_resource.has_value());
  RC_ASSERT(allow_record.policy_outcome.has_value());
  RC_ASSERT(!allow_record.granted_capabilities.empty());
  RC_ASSERT(allow_record.timestamp.time_since_epoch().count() > 0);

  // Test with Deny outcome — should additionally have denial_reason and
  // matching_rule.
  PolicyResult deny_result{PolicyOutcome::kDeny, "Denied by rule", 0};
  layer.AuditAction(session_id, action, deny_result);

  RC_ASSERT(sink.records.size() == 2);
  const auto& deny_record = sink.records[1];
  RC_ASSERT(deny_record.action_type.has_value());
  RC_ASSERT(deny_record.target_resource.has_value());
  RC_ASSERT(deny_record.policy_outcome.has_value());
  RC_ASSERT(deny_record.policy_outcome.value() == PolicyOutcome::kDeny);
  RC_ASSERT(deny_record.denial_reason.has_value());
  RC_ASSERT(!deny_record.denial_reason.value().empty());
  RC_ASSERT(deny_record.matching_rule.has_value());
  RC_ASSERT(!deny_record.matching_rule.value().empty());
}

}  // namespace
}  // namespace shizuru::core
