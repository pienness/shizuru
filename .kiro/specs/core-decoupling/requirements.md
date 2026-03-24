# Requirements Document

## Introduction

This feature decouples two tightly-coupled paths in the Shizuru agent runtime:

**T2 — Tool Call Async (Controller ↔ IoBridge decoupling):** Currently `Controller` holds a `std::unique_ptr<IoBridge>` and `HandleActing` calls `io_->Execute(ac)` synchronously, blocking `loop_thread_` until the tool returns. The goal is to make tool execution a proper async IO round-trip: `HandleActing` emits an `action/tool_call` DataFrame on `action_out`, suspends in `kActing`, and resumes when a `kToolResult` observation arrives via the queue. A new `ToolDispatchDevice` IoDevice handles tool execution and returns results as DataFrames.

**T3 — Control Signal Routing via DataFrame (VadEventDevice ↔ ASR decoupling):** Currently `VadEventDevice` holds a direct function pointer to `asr_ptr->Flush()`, and `CoreDevice` has no `control_out` port. The goal is to route all control signals through the device bus: `CoreDevice` gains a `control_out` port; `Controller` emits control frames on interrupt and response delivery; IO devices gain a `control_in` port and respond to `cancel`/`flush` commands; `VadEventDevice` emits `vad/event` frames that `CoreDevice` converts into `control/flush` commands routed to ASR.

Both changes enforce the existing architectural invariants: `RouteTable` is the single topology source of truth, `CoreDevice` is the only DataFrame ↔ core-types translator, and IO devices are passive.

## Glossary

- **Controller**: The `shizuru::core::Controller` state machine that drives the agent reasoning loop (`core/controller/controller.h/.cpp`).
- **CoreDevice**: The `shizuru::runtime::CoreDevice` IoDevice adapter that wraps `AgentSession` and translates between `DataFrame` and core types (`runtime/core_device.h/.cpp`).
- **AgentRuntime**: The `shizuru::runtime::AgentRuntime` device bus that owns the device registry and `RouteTable`, performs zero data transformation (`runtime/agent_runtime.h/.cpp`).
- **IoBridge**: The `shizuru::core::IoBridge` abstract interface currently used by `Controller` to execute tools synchronously (`core/interfaces/io_bridge.h`).
- **ToolDispatcher**: The existing `shizuru::services::ToolDispatcher` IoBridge implementation (`services/io/tool_dispatcher.h/.cpp`).
- **ToolDispatchDevice**: The new `shizuru::runtime::ToolDispatchDevice` IoDevice to be created (`services/io/tool_dispatch_device.h/.cpp`) that receives `action/tool_call` frames, executes tools via `ToolRegistry`, and emits `action/tool_result` frames.
- **ToolRegistry**: The `shizuru::services::ToolRegistry` map of named tool functions (`services/io/tool_registry.h`).
- **RouteTable**: The `shizuru::runtime::RouteTable` single source of truth for all data flow topology (`runtime/route_table.h`).
- **DataFrame**: The `shizuru::io::DataFrame` typed data packet passed between IoDevices (`io/data_frame.h`).
- **IoDevice**: The `shizuru::io::IoDevice` abstract interface with typed input/output ports (`io/io_device.h`).
- **VadEventDevice**: The `shizuru::io::VadEventDevice` IoDevice that will be refactored to emit `vad/event` DataFrames on `vad_out` instead of firing a direct callback (`io/vad/vad_event_device.h/.cpp`).
- **PolicyLayer**: The `shizuru::core::PolicyLayer` that enforces RBAC/ABAC rules and records audit events. Remains entirely within `core/`; `ToolDispatchDevice` has no access to it (`core/policy/policy_layer.h/.cpp`).
- **BaiduAsrDevice**: The `shizuru::io::BaiduAsrDevice` IoDevice that accumulates audio and transcribes on `Flush()` (`io/asr/baidu/baidu_asr_device.h/.cpp`).
- **ElevenLabsTtsDevice**: The `shizuru::io::ElevenLabsTtsDevice` IoDevice that synthesizes speech from text (`io/tts/elevenlabs/elevenlabs_tts_device.h/.cpp`).
- **AudioPlayoutDevice**: The `shizuru::io::AudioPlayoutDevice` IoDevice that plays audio through PortAudio.
- **InterceptingIoBridge**: The anonymous `InterceptingIoBridge` class inside `core_device.cpp` that currently wraps `IoBridge` to intercept `Execute()` calls and emit DataFrames.
- **control_out**: The new output port to be added to `CoreDevice` for emitting control frames.
- **control_in**: The new input port to be added to IO devices for receiving control commands.
- **action_out**: The existing output port on `CoreDevice` for emitting `action/tool_call` DataFrames.
- **tool_result_in**: The existing input port on `CoreDevice` for receiving `action/tool_result` DataFrames.
- **ControlFrame**: A DataFrame with type `"control/command"` carrying a JSON payload such as `{"command": "cancel"}` or `{"command": "flush"}`.
- **kActing**: The `State::kActing` state in the Controller state machine, entered when a tool call is dispatched.
- **kToolResult**: The `ObservationType::kToolResult` observation type used to deliver tool execution results back to the Controller.
- **CancelCallback**: A `std::function<void()>` injected into `Controller` to replace the `IoBridge::Cancel()` call during interrupt handling.

---

## Requirements

### Requirement 1: Remove IoBridge from Controller

**User Story:** As a runtime architect, I want `Controller` to have no dependency on `IoBridge`, so that the controller is decoupled from tool execution and the loop thread is never blocked by network I/O.

#### Acceptance Criteria

1. THE `Controller` SHALL NOT hold a `std::unique_ptr<IoBridge>` member after this change.
2. WHEN `HandleActing` is called with an `ActionCandidate` of type `kToolCall`, THE `Controller` SHALL emit an `action/tool_call` DataFrame on the `action_out` port and transition to `kActing` state without blocking `loop_thread_`.
3. WHILE in `kActing` state, THE `Controller` SHALL resume the reasoning loop only after a `kToolResult` observation arrives in the observation queue.
4. WHEN a `kToolResult` observation is received while in `kActing` state, THE `Controller` SHALL record the tool result in memory, transition to `kThinking` via `kActionComplete` or `kActionFailed`, and continue the reasoning loop.
5. WHEN `HandleInterrupt` is called while in `kActing` state, THE `Controller` SHALL invoke the injected `CancelCallback` instead of calling `io_->Cancel()`.
6. THE `Controller` constructor SHALL accept a `CancelCallback` (`std::function<void()>`) parameter in place of `std::unique_ptr<IoBridge>`.
7. IF no `CancelCallback` is provided (null), THEN THE `Controller` SHALL treat interrupt cancellation as a no-op without crashing.

---

### Requirement 2: Remove InterceptingIoBridge from CoreDevice and Delete Dead Code

**User Story:** As a runtime architect, I want `CoreDevice` to no longer use `InterceptingIoBridge`, and all code that becomes dead after the IoBridge removal to be deleted, so that the codebase has no unused abstractions.

#### Acceptance Criteria

1. THE `CoreDevice` SHALL NOT instantiate or reference `InterceptingIoBridge` after this change.
2. THE `CoreDevice` constructor SHALL NOT accept a `std::unique_ptr<IoBridge>` parameter after this change.
3. WHEN `Controller` emits an `action/tool_call` DataFrame, THE `CoreDevice` SHALL forward it via its `action_out` port through the registered `OutputCallback` without any additional transformation.
4. THE `CoreDevice` SHALL continue to expose the `action_out` output port with type `"action/tool_call"` in `GetPortDescriptors()`.
5. THE `CoreDevice` SHALL continue to expose the `tool_result_in` input port with type `"action/tool_result"` in `GetPortDescriptors()`.
6. THE file `core/interfaces/io_bridge.h` SHALL be deleted; no remaining code SHALL reference `IoBridge` or `ActionResult`.
7. THE files `services/io/tool_dispatcher.h` and `services/io/tool_dispatcher.cpp` SHALL be deleted; `ToolDispatcher` is the sole `IoBridge` implementation and has no purpose after the interface is removed.
8. THE `ToolDispatcher` compile unit SHALL be removed from its `CMakeLists.txt` target (`shizuru_io_services` or equivalent).

---

### Requirement 3: Implement ToolDispatchDevice

**User Story:** As a runtime architect, I want a `ToolDispatchDevice` IoDevice that executes tools and returns results as DataFrames, so that tool execution is a proper async IO round-trip through the device bus.

#### Acceptance Criteria

1. THE `ToolDispatchDevice` SHALL implement the `IoDevice` interface with device ID `"tool_dispatch"` by default.
2. THE `ToolDispatchDevice` SHALL expose an `action_in` input port with type `"action/tool_call"` and a `result_out` output port with type `"action/tool_result"` in `GetPortDescriptors()`.
3. WHEN an `action/tool_call` DataFrame is received on `action_in`, THE `ToolDispatchDevice` SHALL parse the payload to extract `tool_name` and `arguments`, execute the tool via `ToolRegistry`, and emit an `action/tool_result` DataFrame on `result_out`.
4. THE `ToolDispatchDevice` SHALL execute tool calls on an internal worker thread so that `OnInput` returns immediately without blocking the dispatch thread.
5. IF the tool name is not found in `ToolRegistry`, THEN THE `ToolDispatchDevice` SHALL emit an `action/tool_result` DataFrame with a failure payload indicating the tool was not found.
6. IF tool execution throws an exception, THEN THE `ToolDispatchDevice` SHALL catch the exception, emit an `action/tool_result` DataFrame with a failure payload containing the exception message, and continue processing subsequent frames.
7. THE `ToolDispatchDevice` SHALL accept a `ToolRegistry&` reference in its constructor.
8. WHEN `Stop()` is called, THE `ToolDispatchDevice` SHALL drain the pending task queue and join the worker thread before returning.

---

### Requirement 4: Wire ToolDispatchDevice in AgentRuntime

**User Story:** As a runtime architect, I want `AgentRuntime::StartSession` to register `ToolDispatchDevice` and add the tool call routes, so that the full async tool call round-trip is connected through the device bus.

#### Acceptance Criteria

1. WHEN `StartSession` is called, THE `AgentRuntime` SHALL register a `ToolDispatchDevice` instance with the device bus.
2. WHEN `StartSession` is called, THE `AgentRuntime` SHALL add the route `core:action_out → tool_dispatch:action_in`.
3. WHEN `StartSession` is called, THE `AgentRuntime` SHALL add the route `tool_dispatch:result_out → core:tool_result_in`.
4. THE `AgentRuntime` SHALL NOT pass a `ToolDispatcher` (IoBridge) to `CoreDevice` after this change.
5. WHEN `Shutdown` is called, THE `AgentRuntime` SHALL stop `ToolDispatchDevice` in reverse registration order, consistent with the existing shutdown sequence.

---

### Requirement 5: Define Control Frame Protocol

**User Story:** As a runtime architect, I want a shared header that defines the control frame protocol, so that all devices use a consistent format for control commands.

#### Acceptance Criteria

1. THE `ControlFrame` protocol SHALL be defined in a shared header at `io/control_frame.h`.
2. THE `ControlFrame` header SHALL define the supported command strings: `"cancel"` and `"flush"`.
3. THE `ControlFrame` header SHALL define a helper to serialize a command into a `DataFrame` with type `"control/command"` and a JSON payload `{"command": "<cmd>"}`.
4. THE `ControlFrame` header SHALL define a helper to parse a `"control/command"` DataFrame and extract the command string.
5. IF a `"control/command"` DataFrame contains an unrecognized command, THEN THE receiving device SHALL ignore the frame without logging an error.

---

### Requirement 6: Add control_out Port to CoreDevice

**User Story:** As a runtime architect, I want `CoreDevice` to have a `control_out` output port, so that all control signals originate from `CoreDevice` and are routed through the device bus.

#### Acceptance Criteria

1. THE `CoreDevice` SHALL expose a `control_out` output port with type `"control/command"` in `GetPortDescriptors()`.
2. WHEN `Controller` transitions to `kListening` via `kInterrupt`, THE `CoreDevice` SHALL emit a `ControlFrame` with command `"cancel"` on `control_out`.
3. WHEN `Controller` transitions to `kListening` via `kResponseDelivered`, THE `CoreDevice` SHALL emit a `ControlFrame` with command `"cancel"` on `control_out`.
4. WHEN `Controller` receives a `vad/event` observation with event `"speech_end"`, THE `CoreDevice` SHALL emit a `ControlFrame` with command `"flush"` on `control_out`.
5. THE `CoreDevice` SHALL emit control frames from within the `Controller` transition callback, not from `OnInput`, to preserve the invariant that all control decisions originate from the controller.

---

### Requirement 7: Add control_in Port to IO Devices

**User Story:** As a runtime architect, I want `ElevenLabsTtsDevice`, `AudioPlayoutDevice`, and `BaiduAsrDevice` to each have a `control_in` input port, so that they can receive and respond to control commands routed through the device bus.

#### Acceptance Criteria

1. THE `ElevenLabsTtsDevice` SHALL expose a `control_in` input port with type `"control/command"` in `GetPortDescriptors()`.
2. WHEN a `ControlFrame` with command `"cancel"` is received on `control_in`, THE `ElevenLabsTtsDevice` SHALL call `CancelSynthesis()`.
3. THE `BaiduAsrDevice` SHALL expose a `control_in` input port with type `"control/command"` in `GetPortDescriptors()`.
4. WHEN a `ControlFrame` with command `"flush"` is received on `control_in`, THE `BaiduAsrDevice` SHALL call `Flush()`.
5. WHEN a `ControlFrame` with command `"cancel"` is received on `control_in`, THE `BaiduAsrDevice` SHALL call `CancelTranscription()`.
6. THE `AudioPlayoutDevice` SHALL expose a `control_in` input port with type `"control/command"` in `GetPortDescriptors()`.
7. WHEN a `ControlFrame` with command `"cancel"` is received on `control_in`, THE `AudioPlayoutDevice` SHALL stop the current playout immediately.
8. IF a device receives a `ControlFrame` with an unrecognized command on `control_in`, THEN THE device SHALL ignore the frame without crashing or logging an error.

---

### Requirement 8: Decouple VadEventDevice from ASR

**User Story:** As a runtime architect, I want `VadEventDevice` to emit `vad/event` DataFrames instead of calling `asr_ptr->Flush()` directly, so that VAD-to-ASR control flow passes through `CoreDevice` and the `RouteTable` is the single topology source of truth.

#### Acceptance Criteria

1. THE `VadEventDevice` SHALL expose a `vad_out` output port with type `"vad/event"` in `GetPortDescriptors()`.
2. WHEN a VAD event matching a trigger event is received on `vad_in`, THE `VadEventDevice` SHALL emit a `vad/event` DataFrame on `vad_out` containing the event name, instead of invoking the `EventCallback`.
3. THE `VadEventDevice` constructor SHALL NOT accept an `EventCallback` parameter after this change; the `EventCallback` typedef and `on_event_` / `trigger_events_` members SHALL be removed.
4. THE `CoreDevice` SHALL expose a `vad_in` input port with type `"vad/event"` in `GetPortDescriptors()`.
5. WHEN a `vad/event` DataFrame with event `"speech_end"` is received on `vad_in`, THE `CoreDevice` SHALL emit a `ControlFrame` with command `"flush"` on `control_out`.
6. WHEN `StartSession` is called, THE `AgentRuntime` SHALL add the route `vad_event:vad_out → core:vad_in`.
7. WHEN `StartSession` is called, THE `AgentRuntime` SHALL add the route `core:control_out → baidu_asr:control_in`.

---

### Requirement 9: Permission Checks Remain in Core

**User Story:** As a runtime architect, I want all permission checks and context management to remain inside `Controller` and `PolicyLayer`, so that `ToolDispatchDevice` is a pure executor with no policy awareness.

#### Acceptance Criteria

1. THE `ToolDispatchDevice` SHALL NOT perform any permission checks; it SHALL execute every `action/tool_call` frame it receives unconditionally.
2. THE `Controller` SHALL continue to call `PolicyLayer::CheckPermission` in `HandleRouting` before emitting an `action/tool_call` DataFrame; a denied action SHALL NOT produce an `action/tool_call` frame on `action_out`.
3. WHEN a tool call is denied by policy, THE `Controller` SHALL record the denial in `ContextStrategy` and re-enter `kThinking` via `kRouteToContinue`, exactly as it does today.
4. THE `Controller` SHALL continue to call `PolicyLayer::AuditAction` after a tool result is received, recording the outcome in the audit log.
5. THE `ContextStrategy` SHALL remain the sole owner of conversation history, tool call records, and tool result records; `ToolDispatchDevice` SHALL NOT write to or read from `ContextStrategy`.
6. THE `PolicyLayer` and `ContextStrategy` SHALL remain in the `core/` module; no references to them SHALL appear in `runtime/` or `io/` code outside of `CoreDevice` and `AgentSession`.

---

### Requirement 10: Update Examples and Tests

**User Story:** As a developer, I want all existing examples and tests to be updated to reflect the new decoupled architecture, so that the project builds and all tests pass after the refactor.

#### Acceptance Criteria

1. THE `voice_agent.cpp` example SHALL be updated to register `ToolDispatchDevice` and add the new control routes (`core:control_out → baidu_asr:control_in`, `vad_event:vad_out → core:vad_in`).
2. THE `voice_agent.cpp` example SHALL NOT construct `VadEventDevice` with an `asr_ptr->Flush()` callback after this change.
3. THE `controller_test` SHALL be updated to remove `IoBridge` mocks and test the async tool call round-trip: emit `action/tool_call` observation → verify `kActing` state → enqueue `kToolResult` observation → verify `kThinking` state.
4. THE `core_device_test` SHALL be updated to remove `InterceptingIoBridge` references and verify that `action_out` frames are emitted directly by `Controller`.
5. THE `ToolDispatchDevice` SHALL have unit tests covering: successful tool dispatch, unknown tool name, tool execution exception, and `Stop()` draining the queue.
