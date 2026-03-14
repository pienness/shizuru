# Shizuru

A cross-platform voice conversation agent with an architecture-first design.

## Technology Stack

- C++17: agent framework core + audio runtime (all platforms)
- Flutter: UI layer (desktop + mobile)
- OpenAI compatible API: all LLM calls via HTTP + SSE streaming
- CMake: C++ build system

## Project Structure

```
core/                   # Agent framework: controller, context, policy
llm/                    # LLM client: OpenAI compatible HTTP + SSE
io/                     # IO module (namespace: shizuru::io)
  audio/                #   Audio subsystem (C++)
    audio_device/
      audio_recorder.h  #     Abstract recorder interface
      audio_player.h    #     Abstract player interface
      audio_device.h    #     Facade: StartRecording / StartPlayout
      sample_format.h   #     SampleFormat enum + BytesPerSample
      audio_buffer.h    #     Lock-free ring buffer (template, internal)
      port_audio/       #     PortAudio backend (desktop)
      oboe/             #     Oboe backend (Android, planned)
      core_audio/       #     CoreAudio backend (iOS, planned)
runtime/                # Control plane, data plane, session lifecycle
tests/                  # C++ tests
ui/                     # Flutter app (planned)
```

## Core Architecture

The system uses a decoupled, OS-inspired design:
- Agent Framework Core: `LLM Client + Controller + Context Strategy + Policy`
- Voice System Core: `capture + vad + asr + tts + playout`

Voice capabilities are IO devices/services for the agent, not hard-coded pipeline steps.

## Design Principles

1. Decouple voice pipeline from agent reasoning.
2. Keep decisions in the controller, not in device handlers.
3. Use policy and permission layers as first-class runtime boundaries.
4. Separate control plane (routing, lifecycle) from data plane (audio streams).
5. C++ core is platform-independent; platform-specific code is behind abstract interfaces.
6. Flutter UI has no business logic; it communicates with C++ core via dart:ffi.

## Cross-Platform Support

| Platform      | Audio Backend      | UI      | Agent Core |
|---------------|--------------------|---------|------------|
| macOS / Linux | PortAudio          | Flutter | C++        |
| Windows       | PortAudio / WASAPI | Flutter | C++        |
| Android       | Oboe               | Flutter | C++        |
| iOS           | CoreAudio          | Flutter | C++        |

## Runtime Loop

1. IO.Observation receives user/environment input.
2. Context strategy prepares working context for the LLM.
3. LLM client calls OpenAI compatible API (streaming).
4. Controller chooses route: respond, call IO.Action, or continue planning.
5. Policy/permission checks are enforced before sensitive actions.
6. Voice/media outputs flow through data plane with low-latency constraints.
7. Results are observed, normalized, and fed back into the next cycle.

## TODO

- [ ] Audio: refactor recorder from pull mode (polling ring buffer) to push mode (callback-driven), relying on OS/hardware clock to minimize latency

## Building (C++ core)

```bash
cmake -B build -G Ninja
cmake --build build
```

## Security and Governance

- Least-privilege by default
- Capability-based tool access
- Approval gates for high-risk actions
- Full audit logs for decisions and executions
- Budget and safety guardrails for runtime control
