# Requirements Document

## Introduction

The current `AgentRuntime` class (~380 lines) is a god class that mixes component assembly, multimodal input handling (VAD, STT logic hardcoded), multimodal output handling (TTS logic hardcoded), and session lifecycle management. This redesign decomposes the runtime into a "motherboard" / bus router that only manages data routing between unified IO devices, while preserving the existing `core/` layer (Controller, AgentSession, IoBridge, ContextStrategy, PolicyLayer) without modification.

The key architectural shift: all IO capabilities (TTS, ASR, audio capture, audio playback, tool calls) become IO devices with a uniform interface. The runtime maintains a route table that maps data flows between IO devices and the agent core. Some routes (DMA paths) bypass core entirely for low-latency streaming.

## Glossary

- **Runtime**: The top-level orchestrator that owns the route table and manages IO device lifecycles. Replaces the current `AgentRuntime` god class.
- **IO_Device**: A unified abstraction for any component that produces or consumes data frames. Examples: microphone, speaker, TTS engine, ASR engine, keyboard input, display output, tool dispatcher.
- **DataFrame**: A typed, tagged data unit flowing between IO devices. Carries a payload, a MIME-like type tag, and source/destination metadata.
- **Route_Table**: A declarative mapping of output ports to input ports across IO devices. Determines how data flows through the system.
- **Route**: A single entry in the Route_Table, connecting one device's output port to another device's input port, with optional control-plane gating.
- **DMA_Path**: A route that bypasses the agent core entirely, allowing high-frequency data (e.g., audio) to flow directly between IO devices without controller involvement.
- **Control_Plane**: The low-frequency decision layer where the Runtime evaluates whether a route should be active, based on controller state or policy.
- **Data_Plane**: The high-frequency data flow layer where IO devices exchange DataFrames according to active routes.
- **Core_Device**: A special IO_Device adapter that wraps the existing AgentSession, translating between DataFrame and the core's Observation/ActionCandidate types.
- **Port**: A named input or output endpoint on an IO_Device. Each device exposes one or more output ports and one or more input ports.
- **TTS_Device**: An IO_Device that accepts text DataFrames and produces audio DataFrames.
- **ASR_Device**: An IO_Device that accepts audio DataFrames and produces text DataFrames.
- **IoBridge**: The existing abstract interface in `core/interfaces/io_bridge.h` used by Controller to dispatch IO actions. Preserved unchanged.
- **ToolDispatcher**: The existing IoBridge implementation that dispatches ActionCandidates to registered tool functions. Preserved unchanged.
- **Controller**: The existing state machine in `core/controller/` that drives the agent reasoning loop. Preserved unchanged.

## Requirements

### Requirement 1: IO Device Unified Interface

**User Story:** As a framework developer, I want all IO capabilities to share a single abstract interface, so that the runtime can manage them uniformly without special-casing each modality.

#### Acceptance Criteria

1. THE IO_Device interface SHALL define an `OnInput(DataFrame)` method that accepts incoming data frames.
2. THE IO_Device interface SHALL define a `SetOutputCallback(Callback)` method that registers a callback for outgoing data frames.
3. THE IO_Device interface SHALL define `Start()` and `Stop()` lifecycle methods.
4. THE IO_Device interface SHALL define a `GetDeviceId()` method that returns a unique string identifier for the device instance.
5. THE IO_Device interface SHALL define a `GetPortDescriptors()` method that returns the list of input and output ports the device supports.
6. WHEN `Start()` is called on an IO_Device, THE IO_Device SHALL transition to an active state and begin processing data.
7. WHEN `Stop()` is called on an IO_Device, THE IO_Device SHALL cease processing, release resources, and transition to an inactive state.
8. IF `OnInput(DataFrame)` is called on a stopped IO_Device, THEN THE IO_Device SHALL discard the frame without error.

### Requirement 2: DataFrame Type

**User Story:** As a framework developer, I want a uniform data container for all inter-device communication, so that routes can be defined generically without knowledge of payload specifics.

#### Acceptance Criteria

1. THE DataFrame SHALL contain a `type` field indicating the data kind (e.g., "audio/pcm", "text/plain", "text/json", "action/tool_call").
2. THE DataFrame SHALL contain a `payload` field holding the raw data as a byte buffer.
3. THE DataFrame SHALL contain a `source` field identifying the originating device and port.
4. THE DataFrame SHALL contain a `timestamp` field recording when the frame was created.
5. THE DataFrame SHALL contain an optional `metadata` field for key-value pairs carrying additional context (e.g., sample rate, language code, tool name).

### Requirement 3: Route Table and Routing

**User Story:** As a framework developer, I want the runtime to route data between IO devices based on a declarative route table, so that data flow topology is configurable rather than hardcoded.

#### Acceptance Criteria

1. THE Runtime SHALL maintain a Route_Table that maps source device output ports to destination device input ports.
2. THE Route_Table SHALL support adding routes at runtime via an `AddRoute(source_port, destination_port, options)` method.
3. THE Route_Table SHALL support removing routes at runtime via a `RemoveRoute(source_port, destination_port)` method.
4. WHEN an IO_Device emits a DataFrame on an output port, THE Runtime SHALL deliver that DataFrame to all destination input ports listed in the Route_Table for that output port.
5. WHEN no route exists for a given output port, THE Runtime SHALL discard the emitted DataFrame silently.
6. THE Route_Table SHALL support a `requires_control_plane` flag per route, indicating whether the route requires control-plane evaluation before data delivery.
7. WHILE a route has `requires_control_plane` set to true, THE Runtime SHALL evaluate the control-plane condition before delivering each DataFrame on that route.
8. WHILE a route has `requires_control_plane` set to false (DMA path), THE Runtime SHALL deliver DataFrames directly without control-plane evaluation.

### Requirement 4: DMA Path for Low-Latency Streaming

**User Story:** As a framework developer, I want certain data flows (e.g., TTS audio to speaker) to bypass the agent core entirely, so that real-time audio streaming achieves minimal latency.

#### Acceptance Criteria

1. THE Runtime SHALL support DMA paths where DataFrames flow directly from a source IO_Device to a destination IO_Device without passing through the Core_Device.
2. WHEN a DMA path route is active, THE Runtime SHALL deliver DataFrames with latency no greater than the overhead of a single callback invocation plus any buffering in the destination device.
3. THE Runtime SHALL allow a single source output port to have both a DMA path route and a control-plane-gated route simultaneously (e.g., TTS output to both speaker and core for confirmation).

### Requirement 5: Core Device Adapter

**User Story:** As a framework developer, I want the existing AgentSession to participate in the IO device routing system without modifying core/ code, so that the new architecture wraps the existing core seamlessly.

#### Acceptance Criteria

1. THE Core_Device SHALL implement the IO_Device interface.
2. THE Core_Device SHALL translate incoming text DataFrames into `Observation` objects and enqueue them on the wrapped AgentSession.
3. THE Core_Device SHALL translate `ActionCandidate` outputs from the Controller into outgoing DataFrames emitted on the appropriate output port (e.g., "action/tool_call" for tool calls, "text/plain" for responses).
4. THE Core_Device SHALL preserve the existing IoBridge interface by wrapping the ToolDispatcher, so that Controller's `Execute()` calls continue to work unchanged.
5. THE Core_Device SHALL register the Controller's `OnResponse` callback to capture assistant responses and emit them as DataFrames.
6. IF the Core_Device receives a DataFrame with an unsupported type, THEN THE Core_Device SHALL log a warning and discard the frame.

### Requirement 6: TTS as IO Device

**User Story:** As a framework developer, I want TTS to be a pluggable IO device rather than hardcoded pipeline logic in the runtime, so that TTS vendors can be swapped without changing runtime code.

#### Acceptance Criteria

1. THE TTS_Device interface SHALL extend IO_Device.
2. WHEN the TTS_Device receives a text DataFrame on its input port, THE TTS_Device SHALL synthesize audio and emit audio DataFrames on its output port.
3. THE TTS_Device interface SHALL be vendor-agnostic, defining only generic fields (text, language, voice identifier) without vendor-specific parameters in the interface itself.
4. THE TTS_Device SHALL support cancellation of in-progress synthesis via the `Stop()` method.
5. THE ElevenLabs TTS implementation SHALL implement the TTS_Device interface, adapting the existing `ElevenLabsClient` functionality.

### Requirement 7: ASR as IO Device

**User Story:** As a framework developer, I want ASR to be a pluggable IO device rather than hardcoded STT logic in the runtime, so that ASR vendors can be swapped without changing runtime code.

#### Acceptance Criteria

1. THE ASR_Device interface SHALL extend IO_Device.
2. WHEN the ASR_Device receives an audio DataFrame on its input port, THE ASR_Device SHALL transcribe the audio and emit a text DataFrame on its output port.
3. THE ASR_Device interface SHALL be vendor-agnostic, defining only generic fields (audio format, language hint) without vendor-specific parameters in the interface itself.
4. THE ASR_Device SHALL support cancellation of in-progress transcription via the `Stop()` method.
5. THE Baidu ASR implementation SHALL implement the ASR_Device interface, adapting the existing `BaiduAsrClient` functionality.

### Requirement 8: Runtime as Bus Router

**User Story:** As a framework developer, I want the runtime to act solely as a bus router that manages device lifecycles and data routing, so that it contains zero data transformation logic.

#### Acceptance Criteria

1. THE Runtime SHALL perform zero data transformation on DataFrames passing through routes.
2. THE Runtime SHALL manage the lifecycle of all registered IO devices (calling `Start()` on session begin, `Stop()` on session end).
3. THE Runtime SHALL provide a `RegisterDevice(IO_Device)` method to add devices to the system.
4. THE Runtime SHALL provide an `UnregisterDevice(device_id)` method to remove devices from the system.
5. THE Runtime SHALL wire output callbacks of all registered IO devices to the routing dispatch logic upon device registration.
6. WHEN the Runtime shuts down, THE Runtime SHALL stop all registered IO devices in reverse registration order.
7. THE Runtime SHALL expose a `StartSession()` method that starts all devices and returns a session identifier, preserving the existing public API contract.
8. THE Runtime SHALL expose a `Shutdown()` method that stops all devices and tears down the session.

### Requirement 9: Preserve Core Layer Unchanged

**User Story:** As a framework developer, I want the core/ directory (Controller, AgentSession, IoBridge, ContextStrategy, PolicyLayer) to remain completely unchanged, so that the redesign is purely an outer-layer refactor with zero risk to the reasoning engine.

#### Acceptance Criteria

1. THE redesign SHALL introduce zero modifications to any file under the `core/` directory.
2. THE Core_Device adapter SHALL interact with AgentSession exclusively through its existing public API (`EnqueueObservation`, `GetController().OnResponse`, `Start`, `Shutdown`, `GetState`).
3. THE ToolDispatcher SHALL continue to be passed to AgentSession as the IoBridge implementation, with no changes to ToolDispatcher itself.

### Requirement 10: Directory Structure for IO Abstractions

**User Story:** As a framework developer, I want vendor-agnostic IO device interfaces separated from vendor-specific implementations, so that the codebase maintains a clean abstraction boundary.

#### Acceptance Criteria

1. THE vendor-agnostic TTS_Device interface SHALL reside under `io/tts/`.
2. THE vendor-agnostic ASR_Device interface SHALL reside under `io/asr/`.
3. THE vendor-specific TTS implementations SHALL reside under `services/tts/{vendor}/` (e.g., `services/tts/elevenlabs/`).
4. THE vendor-specific ASR implementations SHALL reside under `services/asr/{vendor}/` (e.g., `services/asr/baidu/`).
5. THE base IO_Device interface and DataFrame type SHALL reside under `io/`.
6. THE existing `io/audio/` directory structure SHALL remain unchanged.

### Requirement 11: Backward-Compatible Public API

**User Story:** As an application developer using Shizuru, I want the new runtime to preserve the essential public API surface (start session, send message, receive output), so that existing example code requires minimal changes.

#### Acceptance Criteria

1. THE new Runtime SHALL expose a `StartSession()` method returning a session ID string.
2. THE new Runtime SHALL expose a `SendMessage(content)` method for text input.
3. THE new Runtime SHALL expose an `OnOutput(callback)` method for receiving assistant responses.
4. THE new Runtime SHALL expose a `Shutdown()` method for session teardown.
5. THE new Runtime SHALL expose a `GetState()` method returning the current controller state.
6. WHEN `SendMessage` is called, THE Runtime SHALL route the text through the appropriate IO device path to reach the Core_Device.
