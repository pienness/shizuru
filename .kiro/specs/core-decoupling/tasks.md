# Implementation Plan: Core Decoupling (T2 + T3)

## Overview

Implement T2 (Controller ↔ IoBridge decoupling via async tool call round-trip) and
T3 (VadEventDevice ↔ ASR decoupling via control frame routing). Tasks are ordered
bottom-up: low-level interface changes first, then device implementations, then
wiring, then tests.

## Tasks

- [x] 1. Define ControlFrame protocol header
  - Create `io/control_frame.h` (header-only) with `ControlFrame::Make` and
    `ControlFrame::Parse` helpers, and `kCommandCancel` / `kCommandFlush` constants.
  - Payload format: `{"command":"<cmd>"}` with type `"control/command"`.
  - `Parse` returns empty string on wrong type or malformed payload.
  - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

  - [x] 1.1 Write property test for ControlFrame round-trip
    - **Property 3: ControlFrame round-trip**
    - **Validates: Requirements 5.3, 5.4**
    - Create `tests/io/control_frame_test.cpp`; use RapidCheck to verify
      `ControlFrame::Parse(ControlFrame::Make(cmd)) == cmd` for arbitrary `cmd`.
    - Also add example tests: wrong type returns empty, malformed payload returns empty.

- [x] 2. Refactor Controller — remove IoBridge, add EmitFrameCallback + CancelCallback
  - In `core/controller/controller.h`: remove `#include "interfaces/io_bridge.h"`,
    remove `std::unique_ptr<IoBridge> io_` member, add `EmitFrameCallback` and
    `CancelCallback` type aliases, add `pending_tool_call_id_` and `pending_action_`
    members.
  - Update constructor signature: replace `std::unique_ptr<IoBridge> io` with
    `EmitFrameCallback emit_frame` and `CancelCallback cancel`.
  - In `core/controller/controller.cpp`:
    - `HandleActing`: serialize `ActionCandidate` to `"<name>:<args>"` payload,
      call `emit_frame_("action_out", frame)` non-blocking, store
      `pending_tool_call_id_` and `pending_action_`, return immediately.
    - `HandleInterrupt`: replace `io_->Cancel()` with `if (cancel_) cancel_();`.
    - `RunLoop`: add `kToolResult` branch — when `state_ == kActing` and
      `obs.type == kToolResult`, call `HandleActingResult(obs)`.
    - Add `HandleActingResult`: records tool result in `ContextStrategy`, calls
      `policy_.AuditAction`, transitions via `kActionComplete`/`kActionFailed`,
      calls `HandleThinking(continuation)`.
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 9.2, 9.3, 9.4_

  - [x] 2.1 Write property test for tool call emit (Property 1)
    - **Property 1: Tool call emit is non-blocking and produces a well-formed frame**
    - **Validates: Requirements 1.2, 2.3**
    - In `tests/agent/controller_prop_test.cpp` (new file): for arbitrary
      `action_name` and `arguments`, verify `EmitFrameCallback` is called exactly
      once with `frame.type == "action/tool_call"` and payload `"<name>:<args>"`,
      and Controller is in `kActing` after `HandleActing` returns.

  - [x] 2.2 Write property test for kToolResult resumption (Property 2)
    - **Property 2: kToolResult observation resumes the reasoning loop**
    - **Validates: Requirements 1.3, 1.4**
    - For arbitrary tool result content string, put Controller in `kActing`,
      enqueue `Observation{kToolResult, content}`, verify transition to `kThinking`.

  - [x] 2.3 Write property test for policy denial suppression (Property 5)
    - **Property 5: Policy denial suppresses action_out emission**
    - **Validates: Requirements 9.2**
    - Configure `PolicyLayer` to deny all; drive Controller to `kRouting` with
      `kToolCall` candidate; verify `EmitFrameCallback` was NOT called with
      `"action/tool_call"` and state is `kThinking`.

- [x] 3. Update AgentSession — mirror Controller constructor change
  - In `core/session/session.h/.cpp`: remove `std::unique_ptr<IoBridge> io`
    parameter, add `core::Controller::EmitFrameCallback emit_frame` and
    `core::Controller::CancelCallback cancel` parameters.
  - Pass them through to `Controller` constructor.
  - _Requirements: 1.6, 2.2_

- [x] 4. Refactor CoreDevice — remove IoBridge + InterceptingIoBridge, add control_out and vad_in
  - In `runtime/core_device.h`: remove `#include "interfaces/io_bridge.h"`, remove
    `IoBridge` constructor parameter, add `kControlOut` and `kVadIn` port constants.
  - In `runtime/core_device.cpp`:
    - Delete the anonymous `InterceptingIoBridge` class entirely.
    - Construct `EmitFrameCallback` lambda (`EmitFrame(port, frame)`) and
      `CancelCallback` lambda (emits `ControlFrame::Make("cancel")` on `control_out`).
    - Pass both to `AgentSession` constructor.
    - Register `OnTransition` callback: on `kListening` via `kInterrupt` or
      `kResponseDelivered`, emit `ControlFrame::Make("cancel")` on `control_out`.
    - `OnInput("vad_in", ...)`: parse payload as event name; if `"speech_end"`,
      emit `ControlFrame::Make("flush")` on `control_out`.
  - Update `GetPortDescriptors()` to include `vad_in` (input, `"vad/event"`) and
    `control_out` (output, `"control/command"`).
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 6.1, 6.2, 6.3, 6.4, 6.5, 8.4, 8.5_

  - [x] 4.1 Write property test for speech_end → flush (Property 7)
    - **Property 7: speech_end on vad_in produces flush on control_out**
    - **Validates: Requirements 6.4, 8.5**
    - In `tests/runtime/core_device_test.cpp`: deliver `vad/event` frame with
      payload `"speech_end"` on `vad_in`; verify `control_out` emits a frame where
      `ControlFrame::Parse(frame) == "flush"`.

  - [x] 4.2 Write property test for interrupt → cancel (Property 8)
    - **Property 8: Interrupt produces cancel on control_out**
    - **Validates: Requirements 1.5, 6.2**
    - For each interruptible state (`kThinking`, `kRouting`, `kActing`), trigger
      interrupt and verify `control_out` emits `"cancel"`.

- [x] 5. Update controller_test to remove IoBridge mocks and add async tool call tests
  - Remove `MockIoBridge` setup from all test fixtures in `tests/agent/controller_test.cpp`.
  - Replace with capturing `EmitFrameCallback` and `CancelCallback` lambdas.
  - Update `TransitionSequence_ToolCallCycle`: verify `kActing` state is entered
    after `EmitFrameCallback` fires, then enqueue `kToolResult` observation and
    verify `kThinking` transition.
  - Add test: `HandleInterrupt` while `kActing` invokes `CancelCallback`.
  - Add edge case: `cancel = nullptr` + `HandleInterrupt` does not crash.
  - _Requirements: 10.3_

- [x] 6. Update core_device_test to remove InterceptingIoBridge references
  - Remove `MockIoBridge` from `MakeCoreDevice` helper in
    `tests/runtime/core_device_test.cpp`.
  - Update `ToolCallActionCandidateToDataFrame`: verify `action_out` frame is
    emitted directly (no IoBridge involved).
  - Add test: `GetPortDescriptors()` contains `vad_in` and `control_out` with
    correct types.
  - Add test: `vad/event "speech_end"` on `vad_in` → `control_out` emits `"flush"`.
  - Add test: `kResponseDelivered` transition → `control_out` emits `"cancel"`.
  - _Requirements: 10.4_

- [x] 7. Checkpoint — ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Delete dead code: io_bridge.h, tool_dispatcher.h/.cpp
  - Delete `core/interfaces/io_bridge.h`.
  - Delete `services/io/tool_dispatcher.h` and `services/io/tool_dispatcher.cpp`.
  - Remove `tool_dispatcher.cpp` from `services/io/CMakeLists.txt` (target
    `shizuru_io_services` or equivalent).
  - Update `services/io/tool_registry.h`: remove `#include "interfaces/io_bridge.h"`;
    replace `core::ActionResult` return type with a local result struct or plain
    `std::string` as appropriate.
  - Verify no remaining `#include` of deleted headers anywhere in the tree.
  - _Requirements: 2.6, 2.7, 2.8_

- [x] 9. Implement ToolDispatchDevice
  - Create `runtime/tool_dispatch_device.h` and `runtime/tool_dispatch_device.cpp`.
  - Ports: `action_in` (input, `"action/tool_call"`), `result_out` (output,
    `"action/tool_result"`).
  - `OnInput`: enqueue `DataFrame` to internal worker queue (non-blocking).
  - `WorkerLoop`: dequeue frames, call `Dispatch(frame)`.
  - `Dispatch`: split payload on first `:` to get `tool_name` and `arguments`;
    look up in `ToolRegistry`; emit success or failure `action/tool_result` frame.
  - Unknown tool → emit `{"success":false,"error":"Unknown tool: <name>"}`.
  - Exception → catch `std::exception` and `...`, emit failure frame, continue.
  - `Stop()`: set `worker_stop_`, notify, join worker thread (drain queue first).
  - Add `ToolDispatchDevice` to `runtime/CMakeLists.txt`.
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

  - [x] 9.1 Write property test for ToolDispatchDevice round-trip (Property 4)
    - **Property 4: ToolDispatchDevice tool call round-trip**
    - **Validates: Requirements 3.3**
    - Create `tests/runtime/tool_dispatch_device_test.cpp`; for arbitrary registered
      tool name and args, verify `result_out` emits a success frame containing the
      tool's return value.

  - [x] 9.2 Write unit tests for ToolDispatchDevice
    - Successful dispatch, unknown tool name, tool throws exception, `Stop()` drains
      queue before joining.
    - _Requirements: 3.5, 3.6, 3.8_

- [x] 10. Wire ToolDispatchDevice in AgentRuntime::StartSession
  - In `runtime/agent_runtime.cpp`: remove `#include "io/tool_dispatcher.h"` and
    `ToolDispatcher` construction; remove `IoBridge` argument from `CoreDevice`
    constructor call.
  - Register `ToolDispatchDevice` instance with the device bus.
  - Add routes:
    - `core:action_out → tool_dispatch:action_in` (control plane)
    - `tool_dispatch:result_out → core:tool_result_in` (control plane)
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [x] 11. Refactor VadEventDevice — remove EventCallback, add vad_out port
  - In `io/vad/vad_event_device.h`: remove `EventCallback` typedef, `on_event_`
    and `trigger_events_` members, and `EventCallback` constructor parameter.
  - New constructor: `explicit VadEventDevice(std::string device_id = "vad_event")`.
  - Add `kVadOut` port constant; update `GetPortDescriptors()` to include `vad_out`
    (output, `"vad/event"`).
  - In `vad_event_device.cpp`: implement `SetOutputCallback` to store the callback;
    `OnInput("vad_in", ...)`: emit a `vad/event` DataFrame on `vad_out` with payload
    = raw event name bytes (emit for every event, not just trigger list).
  - _Requirements: 8.1, 8.2, 8.3_

  - [x] 11.1 Write property test for VadEventDevice pass-through (Property 6)
    - **Property 6: VadEventDevice pass-through emit**
    - **Validates: Requirements 8.2**
    - For arbitrary non-empty event name, deliver `vad/event` frame on `vad_in`;
      verify `vad_out` emits a frame with identical payload bytes.

- [x] 12. Add control_in port to ElevenLabsTtsDevice, BaiduAsrDevice, AudioPlayoutDevice
  - `ElevenLabsTtsDevice` (`io/tts/elevenlabs/elevenlabs_tts_device.h/.cpp`):
    - Add `kControlIn` constant; add `control_in` (input, `"control/command"`) to
      `GetPortDescriptors()`.
    - `OnInput("control_in", ...)`: parse with `ControlFrame::Parse`; `"cancel"` →
      `CancelSynthesis()`; unrecognized → no-op.
  - `BaiduAsrDevice` (`io/asr/baidu/baidu_asr_device.h/.cpp`):
    - Add `kControlIn`; add `control_in` to `GetPortDescriptors()`.
    - `OnInput("control_in", ...)`: `"flush"` → `Flush()`; `"cancel"` →
      `CancelTranscription()`; unrecognized → no-op.
  - `AudioPlayoutDevice` (`io/audio/audio_playout_device.h/.cpp`):
    - Add `kControlIn`; add `control_in` to `GetPortDescriptors()`.
    - `OnInput("control_in", ...)`: `"cancel"` → `player_->Stop()`; unrecognized →
      no-op.
  - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8_

  - [x] 12.1 Write property test for IO device control dispatch (Property 9)
    - **Property 9: IO devices dispatch recognized control commands**
    - **Validates: Requirements 7.2, 7.4, 7.5, 7.7**
    - Create `tests/io/io_device_control_in_test.cpp`; for each (device, command)
      pair in the recognized set, verify the corresponding method is called exactly
      once. Use mock `TtsClient` for ElevenLabs; verify `Flush`/`CancelTranscription`
      on `BaiduAsrDevice`; verify `player_->Stop()` on `AudioPlayoutDevice`.
    - Also verify unrecognized command causes no crash.

- [x] 13. Wire VAD and control routes in AgentRuntime::StartSession
  - In `runtime/agent_runtime.cpp`: add routes:
    - `vad_event:vad_out → core:vad_in` (DMA, `requires_control_plane = false`)
    - `core:control_out → baidu_asr:control_in` (control plane)
    - `core:control_out → elevenlabs_tts:control_in` (control plane)
    - `core:control_out → audio_playout:control_in` (control plane)
  - _Requirements: 8.6, 8.7_

- [x] 14. Update voice_agent.cpp example
  - Remove `VadEventDevice` construction with `asr_ptr->Flush()` callback.
  - Construct `VadEventDevice` with no callback (new default constructor).
  - Register `ToolDispatchDevice` and add the new control routes.
  - _Requirements: 10.1, 10.2_

- [x] 15. Final checkpoint — ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Property tests use RapidCheck; tag each with `// Feature: core-decoupling, Property N: ...`
- Tasks 2–4 must be completed before tasks 5–6 (test updates depend on new interfaces)
- Task 8 (delete dead code) must come after tasks 3–4 confirm no remaining IoBridge consumers
- Task 9 (ToolDispatchDevice) can proceed in parallel with tasks 11–12 (T3 changes)
- `ToolRegistry` return type may need updating in task 8 once `ActionResult` is removed
