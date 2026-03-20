# Implementation Plan: Runtime IO Redesign

## Overview

Decompose the `AgentRuntime` god class into a bus router architecture with uniform IO devices, a declarative route table, and DMA paths. The `core/` directory remains completely unchanged. Implementation proceeds bottom-up: abstractions first, then concrete components, then the rewritten runtime, then adapters, tests, and finally example updates.

## Tasks

- [x] 1. Define IO abstraction layer (`io/io_device.h`, `io/data_frame.h`)
  - [x] 1.1 Create `io/data_frame.h` with the `DataFrame` struct
    - Fields: `type` (string), `payload` (vector<uint8_t>), `source_device`, `source_port`, `timestamp` (steady_clock), `metadata` (unordered_map<string,string>)
    - Namespace: `shizuru::io`
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_
  - [x] 1.2 Create `io/io_device.h` with `PortDescriptor`, `PortDirection`, `OutputCallback`, and `IoDevice` abstract class
    - `IoDevice` pure virtual methods: `GetDeviceId()`, `GetPortDescriptors()`, `OnInput(port_name, DataFrame)`, `SetOutputCallback(OutputCallback)`, `Start()`, `Stop()`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8_

- [x] 2. Define TTS and ASR device interfaces (`io/tts/`, `io/asr/`)
  - [x] 2.1 Create `io/tts/types.h` with vendor-agnostic TTS request/result types
    - Generic fields only: text, language, voice identifier
    - _Requirements: 6.3_
  - [x] 2.2 Create `io/tts/tts_device.h` with `TtsDevice` interface extending `IoDevice`
    - Input port: `text_in`, output port: `audio_out`
    - Add `CancelSynthesis()` virtual method
    - _Requirements: 6.1, 6.2, 6.3, 6.4_
  - [x] 2.3 Create `io/asr/types.h` with vendor-agnostic ASR request/result types
    - Generic fields only: audio format, language hint
    - _Requirements: 7.3_
  - [x] 2.4 Create `io/asr/asr_device.h` with `AsrDevice` interface extending `IoDevice`
    - Input port: `audio_in`, output port: `text_out`
    - Add `CancelTranscription()` virtual method
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

- [x] 3. Implement RouteTable (`runtime/route_table.h`)
  - [x] 3.1 Create `runtime/route_table.h` with `PortAddress`, `PortAddressHash`, `RouteOptions`, `Route`, and `RouteTable` class
    - Methods: `AddRoute`, `RemoveRoute`, `Lookup`, `AllRoutes`, `IsEmpty`
    - `RouteOptions::requires_control_plane` flag for DMA vs gated routes
    - Idempotent `AddRoute`, no-op `RemoveRoute` for non-existent routes
    - _Requirements: 3.1, 3.2, 3.3, 3.6, 3.7, 3.8, 4.1_

  - [x] 3.2 Write property tests for RouteTable
    - [x] 3.2.1 Property test for Property 3: Route Table Add/Remove Round Trip
      - **Property 3: Route Table Add/Remove Round Trip**
      - Generate random PortAddress pairs, verify AddRoute makes Lookup include destination, RemoveRoute makes Lookup exclude it
      - **Validates: Requirements 3.2, 3.3**
    - [x] 3.2.2 Property test for Property 4: Fan-Out Delivery
      - **Property 4: Fan-Out Delivery**
      - Generate random source with N destinations, verify Lookup returns all N
      - **Validates: Requirements 3.4**
    - [x] 3.2.3 Property test for Property 5: Control-Plane Gating vs DMA Bypass
      - **Property 5: Control-Plane Gating vs DMA Bypass**
      - Generate routes with random `requires_control_plane` flags, verify DMA routes are always present in lookup and gated routes carry the correct flag
      - **Validates: Requirements 3.7, 3.8, 4.1**
  - [x] 3.3 Write unit tests for RouteTable
    - Test empty table lookup returns empty vector
    - Test single route add and lookup
    - Test fan-out with 3 destinations
    - Test remove middle route from fan-out
    - Test idempotent AddRoute (duplicate is no-op)
    - Test RemoveRoute on non-existent route (no-op)
    - _Requirements: 3.2, 3.3, 3.4_

- [ ] 4. Checkpoint — Verify IO abstractions and RouteTable
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement CoreDevice adapter (`runtime/core_device.h/.cpp`)
  - [x] 5.1 Create `runtime/core_device.h` with `CoreDevice` class implementing `IoDevice`
    - Port names: `text_in`, `tool_result_in` (inputs), `text_out`, `action_out` (outputs)
    - Constructor takes session dependencies, builds `AgentSession` internally
    - Expose `Session()` and `GetState()` for backward-compatible API access
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_
  - [x] 5.2 Create `runtime/core_device.cpp` with CoreDevice implementation
    - `OnInput("text_in", frame)`: translate DataFrame to `Observation{kUserMessage}` and call `session_->EnqueueObservation()`
    - `OnInput("tool_result_in", frame)`: translate DataFrame to `Observation{kToolResult}` and enqueue
    - Hook `Controller::OnResponse` to emit `text_out` DataFrames for kResponse and `action_out` DataFrames for kToolCall
    - Discard frames on stopped device, log warning for unsupported types
    - Pass `ToolDispatcher` as `IoBridge` to `AgentSession` unchanged
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 9.1, 9.2, 9.3_
  - [x] 5.3 Write property tests for CoreDevice
    - [x] 5.3.1 Property test for Property 6: CoreDevice Text-to-Observation Translation
      - **Property 6: CoreDevice Text-to-Observation Translation**
      - Generate random UTF-8 strings, create text/plain DataFrames, verify CoreDevice enqueues Observation with matching content and kUserMessage type
      - **Validates: Requirements 5.2**
    - [x] 5.3.2 Property test for Property 7: CoreDevice ActionCandidate-to-DataFrame Translation
      - **Property 7: CoreDevice ActionCandidate-to-DataFrame Translation**
      - Generate random ActionCandidates (kResponse and kToolCall), verify emitted DataFrames have correct type tags and payload content
      - **Validates: Requirements 5.3**
    - [x] 5.3.3 Property test for Property 8: CoreDevice Unsupported Type Discard
      - **Property 8: CoreDevice Unsupported Type Discard**
      - Generate DataFrames with random unsupported type tags (not "text/plain" or "action/tool_result"), verify CoreDevice does not emit output and does not throw
      - **Validates: Requirements 5.6**
  - [x] 5.4 Write unit tests for CoreDevice
    - Test specific text message → Observation mapping
    - Test specific tool call ActionCandidate → DataFrame mapping
    - Test unsupported "video/mp4" type is discarded with no output
    - Test tool_result_in port creates kToolResult Observation
    - _Requirements: 5.2, 5.3, 5.6_

- [ ] 6. Checkpoint — Verify CoreDevice
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Rewrite AgentRuntime as bus router (`runtime/agent_runtime.h/.cpp`)
  - [x] 7.1 Rewrite `runtime/agent_runtime.h` with bus router interface
    - Device management: `RegisterDevice(unique_ptr<IoDevice>)`, `UnregisterDevice(device_id)`
    - Route management: `AddRoute`, `RemoveRoute` (delegating to RouteTable)
    - Backward-compatible API: `StartSession()`, `SendMessage()`, `OnOutput()`, `Shutdown()`, `GetState()`
    - Internal: `DispatchFrame()` routing function, `devices_` map, `registration_order_` vector, `core_device_` non-owning pointer
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7, 8.8, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_
  - [x] 7.2 Rewrite `runtime/agent_runtime.cpp` with bus router implementation
    - `RegisterDevice`: store device, wire `SetOutputCallback` to `DispatchFrame`, track registration order
    - `UnregisterDevice`: remove device and its routes
    - `DispatchFrame`: lookup route table, check `requires_control_plane` flag, deliver to destination `OnInput`
    - `StartSession`: create CoreDevice with session deps, register it, wire text_out to output callback, start all devices
    - `SendMessage`: create DataFrame{type="text/plain"} and deliver directly to CoreDevice's text_in
    - `Shutdown`: stop all devices in reverse registration order, clear devices and routes
    - Zero data transformation — frames pass through untouched
    - _Requirements: 3.4, 3.5, 3.7, 3.8, 4.1, 4.2, 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7, 8.8, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_

  - [x] 7.3 Write property tests for AgentRuntime
    - [x] 7.3.1 Property test for Property 2: Device Lifecycle Controls Frame Processing
      - **Property 2: Device Lifecycle Controls Frame Processing**
      - Register MockIoDevices, verify after Start() frames are processed, after Stop() frames are silently discarded
      - **Validates: Requirements 1.6, 1.7, 1.8**
    - [x] 7.3.2 Property test for Property 11: Zero Transformation Invariant
      - **Property 11: Zero Transformation Invariant**
      - Generate random DataFrames with arbitrary type/payload/metadata, route through runtime, verify delivered frame is byte-identical to emitted frame
      - **Validates: Requirements 8.1**
    - [x] 7.3.3 Property test for Property 12: Reverse-Order Shutdown
      - **Property 12: Reverse-Order Shutdown**
      - Register N MockIoDevices in random order, call Shutdown(), verify Stop() is called in reverse registration order
      - **Validates: Requirements 8.6**
    - [x] 7.3.4 Property test for Property 13: SendMessage Routes to CoreDevice
      - **Property 13: SendMessage Routes to CoreDevice**
      - Generate random strings, call SendMessage(), verify CoreDevice receives DataFrame with type="text/plain" and payload matching UTF-8 bytes of input
      - **Validates: Requirements 11.6**
    - [x] 7.3.5 Property test for Property 1: Device ID Uniqueness
      - **Property 1: Device ID Uniqueness**
      - Attempt to register devices with duplicate IDs, verify runtime rejects with std::invalid_argument
      - **Validates: Requirements 1.4**
  - [x] 7.4 Write unit tests for AgentRuntime
    - Test StartSession → SendMessage → verify output callback fires
    - Test Shutdown stops all devices
    - Test GetState returns correct controller state
    - Test RegisterDevice with duplicate ID throws
    - Test frame emitted on port with no routes is silently discarded
    - Test DMA path delivers without control-plane check
    - _Requirements: 8.1, 8.3, 8.4, 8.5, 8.6, 11.1, 11.2, 11.3, 11.4, 11.5_

- [ ] 8. Checkpoint — Verify rewritten AgentRuntime
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Implement ElevenLabs TTS device adapter (`services/tts/elevenlabs/`)
  - [x] 9.1 Create `services/tts/elevenlabs/elevenlabs_tts_device.h` with `ElevenLabsTtsDevice` class
    - Implements `TtsDevice` interface
    - Wraps existing `ElevenLabsClient` for synthesis
    - Device ID: configurable, default "elevenlabs_tts"
    - Ports: `text_in` (input), `audio_out` (output)
    - _Requirements: 6.1, 6.5_
  - [x] 9.2 Create `services/tts/elevenlabs/elevenlabs_tts_device.cpp` with implementation
    - `OnInput("text_in", frame)`: extract text from payload, call `ElevenLabsClient::Synthesize` with streaming callback, emit audio DataFrames on `audio_out`
    - `Start()`/`Stop()`: manage active state, `Stop()` calls `ElevenLabsClient::Cancel()`
    - `CancelSynthesis()`: delegates to `Stop()`
    - Discard frames when stopped
    - _Requirements: 6.2, 6.4, 6.5, 1.8_
  - [x] 9.3 Write property test for TTS device
    - [x] 9.3.1 Property test for Property 9: TTS Device Text-to-Audio Transformation
      - **Property 9: TTS Device Text-to-Audio Transformation**
      - Use a MockTtsClient that returns canned audio, generate random non-empty text DataFrames, verify device emits audio/pcm DataFrames on audio_out
      - **Validates: Requirements 6.2**
  - [x] 9.4 Write unit tests for ElevenLabsTtsDevice
    - Test synthesis with mock client emits audio DataFrames
    - Test CancelSynthesis stops in-progress synthesis
    - Test frame discarded when device is stopped
    - _Requirements: 6.2, 6.4, 6.5_

- [x] 10. Write property test for ASR device (Property 10)
  - [x] 10.1 Property test for Property 10: ASR Device Audio-to-Text Transformation
    - **Property 10: ASR Device Audio-to-Text Transformation**
    - Use a MockAsrDevice implementing `AsrDevice`, generate random audio DataFrames, verify device emits text/plain DataFrames on text_out
    - **Validates: Requirements 7.2**

- [x] 11. Update CMake build system
  - [x] 11.1 Update `runtime/CMakeLists.txt`
    - Add `core_device.cpp` to `shizuru_runtime` sources
    - Add include path for `io/` headers (io_device.h, data_frame.h)
    - _Requirements: 10.5_
  - [x] 11.2 Create `services/tts/elevenlabs/CMakeLists.txt` or update `services/tts/CMakeLists.txt`
    - Add `elevenlabs_tts_device.cpp` to build
    - Link against `shizuru_tts` (existing ElevenLabsClient) and IO headers
    - _Requirements: 10.3_
  - [x] 11.3 Create `tests/runtime/CMakeLists.txt` for runtime tests
    - Add test executables: `route_table_test`, `core_device_test`, `agent_runtime_test`
    - Link against `shizuru_runtime`, `GTest::gtest_main`, `rapidcheck_gtest`
    - Include mock headers directory
    - _Requirements: testing infrastructure_
  - [x] 11.4 Update root `CMakeLists.txt`
    - Add `add_subdirectory(tests/runtime)` for new test directory
    - _Requirements: testing infrastructure_

- [x] 12. Adapt `examples/tool_call.cpp` to new runtime API
  - [x] 12.1 Update `examples/tool_call.cpp`
    - Adapt to any changes in `RuntimeConfig` or `AgentRuntime` constructor
    - Verify `StartSession()`, `SendMessage()`, `OnOutput()`, `Shutdown()` still work with the new bus router implementation
    - Minimal changes expected since backward-compatible API is preserved
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

- [x] 13. Final checkpoint — Full build and test pass
  - Ensure all tests pass, ask the user if questions arise.
  - Verify `core/` directory has zero modifications
  - Verify examples build and link correctly

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests use rapidcheck (already configured in the project) and validate the 13 correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The `core/` directory must remain completely unchanged throughout (Requirement 9)
- All new code uses C++17 and follows existing project conventions (namespaces, header guards, etc.)
