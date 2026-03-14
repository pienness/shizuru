# Implementation Plan: Agent Core Module

## Overview

Incremental implementation of the `core/` module under `shizuru::core` namespace. Builds bottom-up: data types → abstract interfaces → subsystem implementations → aggregate facade → CMake wiring. Each task builds on the previous, and testing tasks are placed close to the implementation they validate. C++17, Google Test + RapidCheck.

## Tasks

- [x] 1. Define data types and enums
  - [x] 1.1 Create `core/controller/types.h` with State, Event, ActionType, ObservationType enums and Observation, ActionCandidate structs
    - Define all enum values per design (State: kIdle..kTerminated, Event: kStart..kRecover, etc.)
    - Define Observation struct (type, content, source, timestamp)
    - Define ActionCandidate struct (type, action_name, arguments, response_text, required_capability)
    - Include `<string>`, `<chrono>` headers
    - _Requirements: 2.1, 3.1, 3.4, 3.5, 3.6_

  - [x] 1.2 Create `core/context/types.h` with MemoryEntryType enum, ContextMessage, ContextWindow, MemoryEntry structs
    - Define MemoryEntryType enum (kUserMessage, kAssistantMessage, kToolCall, kToolResult, kSummary, kExternalContext)
    - Define ContextMessage struct (role, content, tool_call_id, name)
    - Define ContextWindow struct (messages vector, estimated_tokens)
    - Define MemoryEntry struct (type, role, content, source_tag, tool_call_id, timestamp, estimated_tokens)
    - _Requirements: 5.1, 6.1, 6.5_

  - [x] 1.3 Create `core/policy/types.h` with PolicyOutcome enum, PolicyRule, PolicyResult, AuditRecord structs
    - Define PolicyOutcome enum (kAllow, kDeny, kRequireApproval)
    - Define PolicyRule struct (priority, action_pattern, resource_pattern, required_capability, outcome)
    - Define PolicyResult struct (outcome, reason, request_id)
    - Define AuditRecord struct with all optional fields per design (state transition fields, action fields, capabilities snapshot)
    - _Requirements: 9.1, 9.3, 10.1, 10.2, 10.3, 10.5, 10.6_

- [x] 2. Define configuration structs
  - [x] 2.1 Create `core/controller/config.h` with ControllerConfig
    - Fields: max_turns, max_retries, retry_base_delay, wall_clock_timeout, token_budget, action_count_limit
    - Document default values per design
    - _Requirements: 15.1_

  - [x] 2.2 Create `core/context/config.h` with ContextConfig
    - Fields: max_context_tokens, summarization_threshold, default_system_instruction
    - Document default values per design
    - _Requirements: 15.2_

  - [x] 2.3 Create `core/policy/config.h` with PolicyConfig
    - Fields: initial_rules vector, default_capabilities set
    - _Requirements: 15.3_

- [x] 3. Define abstract interfaces
  - [x] 3.1 Create `core/interfaces/llm_client.h` with LlmClient abstract class
    - Define StreamCallback type alias
    - Define LlmResult struct (candidate, prompt_tokens, completion_tokens)
    - Pure virtual methods: Submit(), SubmitStreaming(), Cancel()
    - _Requirements: 13.1_

  - [x] 3.2 Create `core/interfaces/io_bridge.h` with IoBridge abstract class
    - Define ActionResult struct (success, output, error_message)
    - Pure virtual methods: Execute(), Cancel()
    - _Requirements: 13.2_

  - [x] 3.3 Create `core/interfaces/audit_sink.h` with AuditSink abstract class
    - Pure virtual methods: Write(), Flush()
    - _Requirements: 13.3_

  - [x] 3.4 Create `core/interfaces/memory_store.h` with MemoryStore abstract class
    - Pure virtual methods: Append(), GetRecent(), GetAll(), Summarize(), Clear()
    - _Requirements: 13.4_

- [x] 4. Checkpoint - Verify types and interfaces compile
  - Ensure all headers compile cleanly with no errors, ask the user if questions arise.

- [x] 5. Create test infrastructure and mocks
  - [x] 5.1 Create `tests/agent/CMakeLists.txt` that fetches Google Test and RapidCheck, defines test targets
    - FetchContent for googletest and rapidcheck
    - Define test executables for controller, context_strategy, and policy_layer (unit + property)
    - Link against shizuru_core, gtest, rapidcheck
    - _Requirements: 13.6_

  - [x] 5.2 Create mock implementations in `tests/agent/mocks/`
    - `mock_llm_client.h`: MockLlmClient with configurable Submit/Cancel behavior
    - `mock_io_bridge.h`: MockIoBridge with configurable Execute/Cancel behavior
    - `mock_audit_sink.h`: MockAuditSink that captures AuditRecords in a vector
    - `mock_memory_store.h`: MockMemoryStore with in-memory std::vector storage
    - _Requirements: 13.1, 13.2, 13.3, 13.4_

- [x] 6. Implement PolicyLayer
  - [x] 6.1 Create `core/policy/policy_layer.h` and `core/policy/policy_layer.cpp`
    - Constructor accepting PolicyConfig and AuditSink reference
    - InitSession() / ReleaseSession() for per-session capability setup and teardown
    - GrantCapability() / RevokeCapability() / HasCapability() with mutex protection for thread safety
    - CheckPermission() evaluating rules by priority, returning PolicyResult
    - AuditTransition() and AuditAction() creating AuditRecords with monotonic sequence numbers and session_id
    - ResolveApproval() for async approval flow
    - Deny-by-default when no rule matches
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 14.5_

  - [x] 6.2 Write property test: prop_capability_permission_check
    - **Property 18: Capability-based permission check**
    - **Validates: Requirements 8.2, 8.3, 8.5**

  - [x] 6.3 Write property test: prop_capability_grant_revoke
    - **Property 19: Capability grant-revoke round trip**
    - **Validates: Requirements 8.4**

  - [x] 6.4 Write property test: prop_rule_priority_ordering
    - **Property 20: Policy rule priority ordering**
    - **Validates: Requirements 9.1, 9.2**

  - [x] 6.5 Write property test: prop_require_approval_flow
    - **Property 21: RequireApproval suspends and resolves**
    - **Validates: Requirements 9.4, 9.5**

  - [x] 6.6 Write property test: prop_no_match_defaults_deny
    - **Property 22: No matching rule defaults to Deny**
    - **Validates: Requirements 9.6**

  - [x] 6.7 Write property test: prop_audit_record_invariants
    - **Property 23: Audit record invariants**
    - **Validates: Requirements 10.5, 10.6**

  - [x] 6.8 Write property test: prop_transition_audit_completeness
    - **Property 24: Transition audit record completeness**
    - **Validates: Requirements 10.1**

  - [x] 6.9 Write property test: prop_action_audit_completeness
    - **Property 25: Action audit record completeness**
    - **Validates: Requirements 10.2, 10.3**

  - [x] 6.10 Write unit tests for PolicyLayer in `tests/agent/policy_layer_test.cpp`
    - Test specific rule matching examples
    - Test approval flow (request → approve, request → deny)
    - Test capability grant/revoke sequences
    - Test audit record field verification
    - Test deny-by-default behavior
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 10.1, 10.2, 10.3, 10.4, 10.5, 10.6_

- [x] 7. Implement ContextStrategy
  - [x] 7.1 Create `core/context/context_strategy.h` and `core/context/context_strategy.cpp`
    - Constructor accepting ContextConfig and MemoryStore reference
    - InitSession() storing system instruction (or default) per session
    - BuildContext() assembling ContextWindow: system message first, memory in chronological order, current Observation last
    - Token budget enforcement: truncate oldest MemoryEntries first, never truncate system message or current Observation
    - Tool call/result adjacency: keep tool_call and tool_result messages paired
    - RecordTurn() and InjectContext() for memory management
    - SetSystemInstruction() for mid-session updates
    - ReleaseSession() clearing all session data
    - MaybeSummarize() triggered when entry count exceeds summarization_threshold
    - EstimateTokens() helper
    - Mutex protection on system_instructions_ map
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.3, 6.4, 6.5, 7.1, 7.2, 7.3, 7.4, 14.4_

  - [x] 7.2 Write property test: prop_context_ordering
    - **Property 10: Context window ordering invariant**
    - **Validates: Requirements 5.1, 5.2, 7.2**

  - [x] 7.3 Write property test: prop_context_token_budget
    - **Property 11: Context window token budget with preservation**
    - **Validates: Requirements 5.3, 5.4**

  - [x] 7.4 Write property test: prop_tool_call_result_adjacency
    - **Property 12: Tool call and result adjacency**
    - **Validates: Requirements 5.5**

  - [x] 7.5 Write property test: prop_memory_round_trip
    - **Property 13: Memory entry round-trip**
    - **Validates: Requirements 6.1, 6.5**

  - [x] 7.6 Write property test: prop_summarization_threshold
    - **Property 14: Memory summarization threshold**
    - **Validates: Requirements 6.2**

  - [x] 7.7 Write property test: prop_recent_memory_retrieval
    - **Property 15: Recent memory retrieval**
    - **Validates: Requirements 6.3**

  - [x] 7.8 Write property test: prop_session_release_clears_memory
    - **Property 16: Session termination releases memory**
    - **Validates: Requirements 6.4**

  - [x] 7.9 Write property test: prop_system_instruction_update
    - **Property 17: System instruction update takes effect immediately**
    - **Validates: Requirements 7.3**

  - [x] 7.10 Write unit tests for ContextStrategy in `tests/agent/context_strategy_test.cpp`
    - Test context window assembly with known inputs
    - Test token budget edge cases (budget exactly equals content)
    - Test tool call/result pairing
    - Test summarization trigger at exact threshold
    - Test default system instruction fallback
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.3, 6.4, 6.5, 7.1, 7.2, 7.3, 7.4_

- [x] 8. Checkpoint - Verify PolicyLayer and ContextStrategy
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Implement Controller
  - [x] 9.1 Create `core/controller/controller.h` and `core/controller/controller.cpp`
    - Static transition table as `std::unordered_map<std::pair<State,Event>, State, PairHash>` with all 24 transitions from design
    - Constructor accepting ControllerConfig, unique_ptr<LlmClient>, unique_ptr<IoBridge>, ContextStrategy&, PolicyLayer&
    - TryTransition(): validate (state, event) pair in table, execute on-exit/on-enter callbacks, update atomic state, emit diagnostic on invalid transition
    - EnqueueObservation(): thread-safe push to deque with mutex + condition_variable notify
    - Start(): launch loop_thread_ running RunLoop()
    - Shutdown(): set shutdown_requested_, notify queue_cv_, join thread, transition to Terminated
    - GetState(): return atomic state value
    - OnTransition() / OnDiagnostic(): register callbacks
    - RunLoop(): main loop dequeuing observations, driving state machine through Thinking→Routing→Acting/Responding cycle
    - HandleThinking(): call ContextStrategy::BuildContext(), submit to LlmClient, handle retry with exponential backoff, track token counts
    - HandleRouting(): inspect ActionCandidate.type, check PolicyLayer::CheckPermission(), transition accordingly
    - HandleActing(): call IoBridge::Execute(), record result as Observation, transition back to Thinking
    - HandleResponding(): deliver response, check stop conditions, transition to Listening or Idle
    - CheckBudget(): enforce max_turns, token_budget, action_count_limit, wall_clock_timeout
    - HandleInterrupt(): cancel in-progress LLM/IO, record partial results, transition to Listening
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 1.10, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 12.1, 12.2, 12.3, 12.4, 12.5, 13.5, 14.1, 14.2, 14.3_

  - [x] 9.2 Write property test: prop_initial_state_is_idle
    - **Property 1: Initial state is Idle**
    - **Validates: Requirements 1.1**

  - [x] 9.3 Write property test: prop_valid_transitions
    - **Property 2: Valid transitions produce correct next state**
    - **Validates: Requirements 1.2, 1.3, 1.5, 1.7, 1.8, 1.9, 2.3, 4.5, 12.3**

  - [x] 9.4 Write property test: prop_invalid_transitions_preserve_state
    - **Property 3: Invalid transitions preserve state**
    - **Validates: Requirements 1.10, 2.4**

  - [x] 9.5 Write property test: prop_transition_callbacks_order
    - **Property 4: Transition callbacks fire in order**
    - **Validates: Requirements 2.5**

  - [x] 9.6 Write property test: prop_action_routing_by_type
    - **Property 5: Action routing is determined by ActionType**
    - **Validates: Requirements 1.6, 3.4, 3.5, 3.6**

  - [x] 9.7 Write property test: prop_observation_fifo
    - **Property 6: Observation queue preserves FIFO order**
    - **Validates: Requirements 3.1**

  - [x] 9.8 Write property test: prop_turn_count_stop
    - **Property 7: Turn count stop condition**
    - **Validates: Requirements 3.7, 3.8, 1.8**

  - [x] 9.9 Write property test: prop_llm_retry_backoff
    - **Property 8: LLM retry with exponential backoff**
    - **Validates: Requirements 4.1, 4.2, 4.3**

  - [x] 9.10 Write property test: prop_io_failure_feeds_thinking
    - **Property 9: IO action failure feeds back to Thinking**
    - **Validates: Requirements 4.4**

  - [x] 9.11 Write property test: prop_budget_guardrails
    - **Property 26: Budget guardrails terminate the loop**
    - **Validates: Requirements 11.1, 11.2, 11.3, 11.4**

  - [x] 9.12 Write property test: prop_interruption_behavior
    - **Property 27: Interruption cancels in-progress work and preserves context**
    - **Validates: Requirements 12.1, 12.2, 12.4, 12.5**

  - [x] 9.13 Write unit tests for Controller in `tests/agent/controller_test.cpp`
    - Test session lifecycle (create → start → shutdown)
    - Test specific transition sequences
    - Test error recovery flow (Error → recover → Idle)
    - Test budget exceeded scenarios
    - Test wall-clock timeout behavior
    - Test interrupt during Thinking/Routing/Acting
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 1.10, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 12.1, 12.2, 12.3, 12.4, 12.5_

- [x] 10. Checkpoint - Verify Controller implementation
  - Ensure all tests pass, ask the user if questions arise.

- [x] 11. Implement AgentSession facade and CMake build
  - [x] 11.1 Create `core/session/session.h` and `core/session/session.cpp`
    - Constructor wiring ContextStrategy, PolicyLayer, and Controller together
    - Owns unique_ptr<MemoryStore> and unique_ptr<AuditSink>
    - Delegates Start(), Shutdown(), EnqueueObservation(), GetState() to Controller
    - Calls InitSession() on ContextStrategy and PolicyLayer during construction
    - Calls ReleaseSession() on ContextStrategy and PolicyLayer during destruction/shutdown
    - _Requirements: 1.1, 1.9, 6.4, 13.5_

  - [x] 11.2 Create `core/CMakeLists.txt`
    - Define `shizuru_core` static library with controller.cpp, context_strategy.cpp, policy_layer.cpp, session.cpp
    - Set `target_include_directories` to `${CMAKE_CURRENT_SOURCE_DIR}` (flat layout matching io/audio pattern)
    - No external dependencies (interfaces only)
    - _Requirements: 13.6_

  - [x] 11.3 Wire `core/` into root `CMakeLists.txt` via `add_subdirectory(core)`
    - Add `add_subdirectory(core)` to root CMakeLists.txt
    - Add `add_subdirectory(tests/agent)` to root CMakeLists.txt
    - _Requirements: 13.6_

- [x] 12. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document (Properties 1-27)
- Unit tests validate specific examples and edge cases
- All code lives under `shizuru::core` namespace, flat layout matching `io/audio/` pattern
- RapidCheck generators for State, Event, Observation, ActionCandidate, MemoryEntry, PolicyRule, and config structs should be defined in test helper headers as needed
