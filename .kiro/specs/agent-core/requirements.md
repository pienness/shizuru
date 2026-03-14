# Requirements Document: Agent Core Module

## Introduction

The Agent Core module (`core/`) is the brain of the Shizuru voice conversation agent. It provides three foundational subsystems: the Controller (state machine for lifecycle and routing), the Context Strategy (memory and prompt orchestration), and the Permission & Policy Layer (security boundaries and audit). Together, these subsystems drive the agent's reasoning loop — receiving observations, building context, invoking the LLM, routing decisions, and enforcing governance — all in C++17 under the `shizuru::core` namespace.

This module is platform-independent and consumed by the `runtime/` module (control plane + data plane). It interacts with the `io/` module (IO.Observation and IO.Action) and the `llm/` module (OpenAI compatible HTTP+SSE client) through abstract interfaces.

## Glossary

- **Controller**: The state machine component that manages agent lifecycle, plans next steps, routes LLM outputs to actions or responses, handles retries, and evaluates stop conditions.
- **Context_Strategy**: The component responsible for assembling the LLM prompt from conversation history, system instructions, retrieved memory, and observation data.
- **Policy_Layer**: The permission and policy enforcement component that governs which actions the Controller and IO.Action may execute, based on capability rules, approval gates, and audit requirements.
- **Agent_Session**: A single continuous interaction session between the user and the agent, from initialization to termination.
- **Observation**: An input event received from the external environment (user utterance, tool result, system event) via IO.Observation.
- **Action_Candidate**: An action proposed by the LLM (e.g., respond to user, call a tool, continue planning) that the Controller must route.
- **Context_Window**: The assembled sequence of messages and metadata sent to the LLM for a single inference call.
- **Policy_Rule**: A declarative rule that specifies whether a given action is permitted, denied, or requires approval.
- **Capability**: A named permission token that grants access to a specific IO.Action or resource.
- **Audit_Record**: A structured log entry capturing a decision, action execution, or policy evaluation for traceability.
- **Stop_Condition**: A predicate evaluated by the Controller to determine whether the reasoning loop should terminate.
- **Turn**: One complete cycle of the reasoning loop: observe → build context → LLM inference → route → execute → observe result.
- **Memory_Entry**: A stored piece of information (conversation turn, summary, fact) that the Context_Strategy may retrieve and inject into the Context_Window.
- **LLM_Client**: The external module (`llm/`) that provides OpenAI compatible HTTP+SSE streaming inference. The Core module depends on its abstract interface but does not own it.

## Requirements

### Requirement 1: Agent Session Lifecycle

**User Story:** As a runtime module, I want the Controller to manage a well-defined session lifecycle, so that the agent starts, runs, and terminates in a predictable and observable manner.

#### Acceptance Criteria

1. WHEN an Agent_Session is created, THE Controller SHALL initialize in the `Idle` state.
2. WHEN a start command is received while in the `Idle` state, THE Controller SHALL transition to the `Listening` state.
3. WHILE in the `Listening` state, THE Controller SHALL accept Observations and transition to the `Thinking` state upon receiving a user Observation.
4. WHILE in the `Thinking` state, THE Controller SHALL invoke the Context_Strategy to build a Context_Window and submit it to the LLM_Client.
5. WHEN the LLM_Client returns an Action_Candidate, THE Controller SHALL transition to the `Routing` state.
6. WHILE in the `Routing` state, THE Controller SHALL evaluate the Action_Candidate and transition to `Acting` (if an IO.Action is needed), `Responding` (if a direct response is ready), or back to `Thinking` (if further planning is needed).
7. WHEN an IO.Action execution completes, THE Controller SHALL transition from `Acting` back to `Thinking` to process the action result.
8. WHEN a Stop_Condition is met, THE Controller SHALL transition to the `Idle` state and emit the final response.
9. WHEN a shutdown command is received, THE Controller SHALL transition to the `Terminated` state regardless of current state, releasing all resources owned by the Agent_Session.
10. THE Controller SHALL reject state transitions that are not defined in the state machine and log the rejected transition attempt.

### Requirement 2: State Machine Integrity

**User Story:** As a developer, I want the Controller state machine to be well-defined and verifiable, so that the agent never enters an undefined or inconsistent state.

#### Acceptance Criteria

1. THE Controller SHALL define a finite set of states: `Idle`, `Listening`, `Thinking`, `Routing`, `Acting`, `Responding`, `Error`, `Terminated`.
2. THE Controller SHALL define all valid transitions as a static transition table mapping (current_state, event) pairs to next_state values.
3. WHEN a transition is triggered, THE Controller SHALL execute the transition only if the (current_state, event) pair exists in the transition table.
4. IF a transition is attempted for an undefined (current_state, event) pair, THEN THE Controller SHALL remain in the current state and emit a diagnostic event.
5. WHEN a transition occurs, THE Controller SHALL invoke registered transition callbacks (on-exit for the old state, on-enter for the new state) in that order.
6. THE Controller SHALL expose the current state through a thread-safe accessor.

### Requirement 3: Reasoning Loop Execution

**User Story:** As a runtime module, I want the Controller to drive the observe-think-act loop, so that the agent processes inputs and produces outputs through a consistent cycle.

#### Acceptance Criteria

1. WHEN an Observation is received, THE Controller SHALL enqueue the Observation for processing in FIFO order.
2. WHILE in the `Thinking` state, THE Controller SHALL invoke the Context_Strategy to produce a Context_Window from the current Agent_Session state.
3. WHILE in the `Thinking` state, THE Controller SHALL submit the Context_Window to the LLM_Client and await the Action_Candidate.
4. WHEN the LLM_Client returns an Action_Candidate of type "tool_call", THE Controller SHALL route the Action_Candidate to IO.Action execution via the `Acting` state.
5. WHEN the LLM_Client returns an Action_Candidate of type "response", THE Controller SHALL route the Action_Candidate to the `Responding` state for delivery.
6. WHEN the LLM_Client returns an Action_Candidate of type "continue", THE Controller SHALL remain in or re-enter the `Thinking` state for further planning.
7. THE Controller SHALL enforce a configurable maximum Turn count per Agent_Session as a Stop_Condition.
8. WHEN the maximum Turn count is reached, THE Controller SHALL terminate the reasoning loop and emit a timeout response.

### Requirement 4: Retry and Error Recovery

**User Story:** As a runtime module, I want the Controller to handle transient failures gracefully, so that the agent can recover from LLM or IO errors without crashing.

#### Acceptance Criteria

1. IF the LLM_Client returns a transient error (e.g., timeout, rate limit), THEN THE Controller SHALL retry the inference call up to a configurable maximum retry count.
2. WHEN retrying an LLM inference call, THE Controller SHALL apply exponential backoff with a configurable base delay.
3. IF the maximum retry count is exceeded for an LLM call, THEN THE Controller SHALL transition to the `Error` state and emit a failure diagnostic.
4. IF an IO.Action execution fails, THEN THE Controller SHALL record the failure as an Observation and transition back to `Thinking` so the LLM can decide the next step.
5. WHILE in the `Error` state, THE Controller SHALL allow a recovery command to transition back to `Idle` or a shutdown command to transition to `Terminated`.
6. IF an unhandled exception occurs during any state, THEN THE Controller SHALL catch the exception, transition to the `Error` state, and log the exception details.

### Requirement 5: Context Window Assembly

**User Story:** As a developer, I want the Context_Strategy to assemble a well-structured prompt for the LLM, so that the agent has the right information to reason about the current situation.

#### Acceptance Criteria

1. THE Context_Strategy SHALL assemble a Context_Window containing: system instructions, relevant Memory_Entries, and the current Turn's Observation.
2. THE Context_Strategy SHALL order Context_Window messages as: system message first, then memory/history messages in chronological order, then the current Observation last.
3. WHEN assembling the Context_Window, THE Context_Strategy SHALL enforce a configurable maximum token budget and truncate older Memory_Entries first if the budget is exceeded.
4. THE Context_Strategy SHALL preserve the system message and the current Observation from truncation (these are always included).
5. WHEN a tool call result Observation is received, THE Context_Strategy SHALL include the original tool call message and its result as adjacent messages in the Context_Window.

### Requirement 6: Conversation Memory Management

**User Story:** As a developer, I want the Context_Strategy to manage conversation memory, so that the agent maintains coherent multi-turn conversations.

#### Acceptance Criteria

1. THE Context_Strategy SHALL store each completed Turn (user message, assistant response, tool calls and results) as Memory_Entries in the Agent_Session.
2. WHEN the total Memory_Entry count exceeds a configurable threshold, THE Context_Strategy SHALL summarize older entries into a condensed Memory_Entry to reduce token usage.
3. THE Context_Strategy SHALL provide a method to retrieve the N most recent Memory_Entries for a given Agent_Session.
4. WHEN an Agent_Session transitions to `Terminated`, THE Context_Strategy SHALL release all Memory_Entries associated with that session.
5. THE Context_Strategy SHALL support injecting external context (e.g., retrieved documents, user profile data) as Memory_Entries with a designated source tag.

### Requirement 7: System Instruction Management

**User Story:** As a developer, I want to configure the agent's system instructions, so that the agent's persona and behavior can be customized per session.

#### Acceptance Criteria

1. THE Context_Strategy SHALL accept a system instruction string during Agent_Session initialization.
2. THE Context_Strategy SHALL place the system instruction as the first message in every Context_Window.
3. WHEN the system instruction is updated mid-session, THE Context_Strategy SHALL use the updated instruction for all subsequent Context_Window assemblies.
4. IF no system instruction is provided, THEN THE Context_Strategy SHALL use a configurable default system instruction.

### Requirement 8: Capability-Based Action Permissions

**User Story:** As a runtime module, I want the Policy_Layer to enforce capability-based permissions on IO.Actions, so that the agent operates under least-privilege constraints.

#### Acceptance Criteria

1. THE Policy_Layer SHALL maintain a set of granted Capabilities for each Agent_Session.
2. WHEN the Controller routes an Action_Candidate to IO.Action execution, THE Policy_Layer SHALL verify that the required Capability is present in the session's granted set.
3. IF the required Capability is not granted, THEN THE Policy_Layer SHALL deny the action and return a denial reason to the Controller.
4. THE Policy_Layer SHALL support granting and revoking Capabilities at runtime without restarting the Agent_Session.
5. THE Policy_Layer SHALL default to denying all actions for which no explicit Capability is granted (deny-by-default).

### Requirement 9: Policy Rule Evaluation

**User Story:** As a developer, I want to define declarative policy rules, so that action permissions can be configured without code changes.

#### Acceptance Criteria

1. THE Policy_Layer SHALL evaluate Policy_Rules as ordered (priority, rule) pairs, where the first matching rule determines the outcome.
2. WHEN evaluating a Policy_Rule, THE Policy_Layer SHALL match against the action type, the target resource, and the session's Capabilities.
3. THE Policy_Layer SHALL support three rule outcomes: `Allow`, `Deny`, and `RequireApproval`.
4. WHEN a Policy_Rule evaluates to `RequireApproval`, THE Policy_Layer SHALL suspend the action execution and emit an approval request event.
5. WHEN an approval response is received for a suspended action, THE Policy_Layer SHALL resume or deny the action based on the approval response.
6. IF no Policy_Rule matches a given action, THEN THE Policy_Layer SHALL deny the action (closed-world assumption).

### Requirement 10: Audit Logging

**User Story:** As an operator, I want all agent decisions and actions to be auditable, so that I can trace what the agent did and why.

#### Acceptance Criteria

1. WHEN the Controller performs a state transition, THE Policy_Layer SHALL create an Audit_Record containing the previous state, the new state, the triggering event, and a timestamp.
2. WHEN an IO.Action is executed, THE Policy_Layer SHALL create an Audit_Record containing the action type, target resource, granted Capabilities, policy evaluation result, and a timestamp.
3. WHEN a Policy_Rule evaluation results in `Deny`, THE Policy_Layer SHALL create an Audit_Record containing the denied action, the matching rule, and the denial reason.
4. THE Policy_Layer SHALL write Audit_Records to a configurable audit sink (e.g., in-memory buffer, callback, or log stream).
5. THE Policy_Layer SHALL assign a monotonically increasing sequence number to each Audit_Record within an Agent_Session.
6. THE Policy_Layer SHALL include the Agent_Session identifier in every Audit_Record.

### Requirement 11: Budget and Safety Guardrails

**User Story:** As an operator, I want to set budget and safety limits on agent behavior, so that the agent cannot consume excessive resources or perform unsafe operations.

#### Acceptance Criteria

1. THE Controller SHALL track the cumulative token count (prompt + completion) across all LLM calls within an Agent_Session.
2. WHEN the cumulative token count exceeds a configurable budget limit, THE Controller SHALL terminate the reasoning loop and emit a budget-exceeded response.
3. THE Controller SHALL track the cumulative number of IO.Action executions within an Agent_Session.
4. WHEN the cumulative IO.Action count exceeds a configurable limit, THE Controller SHALL terminate the reasoning loop and emit an action-limit-exceeded response.
5. THE Controller SHALL enforce a configurable wall-clock timeout for the entire Agent_Session.
6. WHEN the wall-clock timeout is reached, THE Controller SHALL terminate the reasoning loop regardless of current state and emit a timeout response.

### Requirement 12: Interruption Handling

**User Story:** As a runtime module, I want the Controller to support interruption, so that the agent can stop its current activity when the user provides new input mid-turn.

#### Acceptance Criteria

1. WHEN a new user Observation arrives while the Controller is in the `Thinking`, `Routing`, or `Acting` state, THE Controller SHALL mark the current Turn as interrupted.
2. WHEN a Turn is marked as interrupted, THE Controller SHALL cancel or abandon the in-progress LLM call or IO.Action as soon as possible.
3. WHEN a Turn is interrupted, THE Controller SHALL transition to `Listening` to process the new Observation.
4. THE Controller SHALL record the interrupted Turn and its partial results as Memory_Entries so the Context_Strategy can include interruption context.
5. THE Controller SHALL emit a diagnostic event when a Turn is interrupted, including the interrupted state and the reason.

### Requirement 13: Abstract Interface Boundaries

**User Story:** As a developer, I want the Core module to depend on abstract interfaces for LLM and IO, so that the module is testable and decoupled from concrete implementations.

#### Acceptance Criteria

1. THE Controller SHALL depend on an abstract LLM_Client interface for submitting Context_Windows and receiving Action_Candidates.
2. THE Controller SHALL depend on an abstract IO_Bridge interface for dispatching IO.Action requests and receiving IO.Observation events.
3. THE Policy_Layer SHALL depend on an abstract Audit_Sink interface for writing Audit_Records.
4. THE Context_Strategy SHALL depend on an abstract Memory_Store interface for persisting and retrieving Memory_Entries.
5. THE Controller SHALL accept all abstract dependencies via constructor injection.
6. THE Core module SHALL compile and link independently of any concrete LLM, IO, or storage implementation.

### Requirement 14: Thread Safety and Concurrency

**User Story:** As a runtime module, I want the Core module to be safe for use in a multi-threaded environment, so that observations can arrive from IO threads while the reasoning loop runs on its own thread.

#### Acceptance Criteria

1. THE Controller SHALL provide a thread-safe method for enqueuing Observations from any thread.
2. THE Controller SHALL process enqueued Observations on the reasoning loop thread only.
3. THE Controller SHALL protect state transitions with synchronization so that concurrent transition attempts are serialized.
4. THE Context_Strategy SHALL protect Memory_Entry storage with synchronization so that concurrent reads and writes are safe.
5. THE Policy_Layer SHALL be safe to call from the reasoning loop thread and from external threads requesting Capability changes.

### Requirement 15: Configuration

**User Story:** As a developer, I want all tunable parameters of the Core module to be configurable, so that behavior can be adjusted without recompilation.

#### Acceptance Criteria

1. THE Controller SHALL accept a configuration struct at construction time containing: maximum Turn count, maximum retry count, retry base delay, wall-clock timeout, token budget limit, and action count limit.
2. THE Context_Strategy SHALL accept a configuration struct at construction time containing: maximum token budget for Context_Window, memory summarization threshold, and default system instruction.
3. THE Policy_Layer SHALL accept a configuration struct at construction time containing: default Capability set and initial Policy_Rules.
4. WHEN a configuration value is not explicitly provided, THE Core module SHALL use a documented default value.
