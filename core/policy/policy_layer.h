#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "controller/types.h"
#include "interfaces/audit_sink.h"
#include "policy/config.h"
#include "policy/types.h"

namespace shizuru::core {

class PolicyLayer {
 public:
  PolicyLayer(PolicyConfig config, AuditSink& sink);

  // Check if an action is permitted for the given session.
  PolicyResult CheckPermission(const std::string& session_id,
                               const ActionCandidate& action);

  // Grant a capability to a session (thread-safe).
  void GrantCapability(const std::string& session_id,
                       const std::string& capability);

  // Revoke a capability from a session (thread-safe).
  void RevokeCapability(const std::string& session_id,
                        const std::string& capability);

  // Check if a session has a specific capability.
  bool HasCapability(const std::string& session_id,
                     const std::string& capability) const;

  // Record a state transition audit event.
  void AuditTransition(const std::string& session_id,
                       State from, State to, Event event);

  // Record an action execution audit event.
  void AuditAction(const std::string& session_id,
                   const ActionCandidate& action,
                   PolicyResult result);

  // Resolve a pending approval.
  void ResolveApproval(const std::string& session_id,
                       uint64_t request_id, bool approved);

  // Initialize session with default capabilities.
  void InitSession(const std::string& session_id);

  // Release session data.
  void ReleaseSession(const std::string& session_id);

 private:
  PolicyResult EvaluateRules(const std::string& session_id,
                             const ActionCandidate& action);

  uint64_t NextSequenceNumber(const std::string& session_id);

  PolicyConfig config_;
  AuditSink& sink_;

  // Per-session capabilities (thread-safe access)
  mutable std::mutex cap_mutex_;
  std::unordered_map<std::string, std::unordered_set<std::string>> capabilities_;

  // Per-session audit sequence counters
  std::mutex seq_mutex_;
  std::unordered_map<std::string, uint64_t> sequence_numbers_;

  // Pending approvals
  std::mutex approval_mutex_;
  std::unordered_map<uint64_t, std::function<void(bool)>> pending_approvals_;

  // Counter for generating unique approval request IDs
  std::mutex request_id_mutex_;
  uint64_t next_request_id_ = 1;
};

}  // namespace shizuru::core
