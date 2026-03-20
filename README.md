# Shizuru

A cross-platform voice conversation agent built in C++17, with a Flutter UI and OpenAI-compatible LLM backend.

## Requirements

- CMake 3.20+
- C++17 compiler (clang or gcc)
- Ninja (recommended)
- OpenSSL
- PortAudio (desktop audio)

On macOS:
```bash
brew install cmake ninja openssl portaudio
```

## Build

```bash
cmake -B build -G Ninja
cmake --build build
```

## Run

```bash
# Built-in mock LLM server — no API key needed
./build/examples/tool_call_example

# Local Ollama
./build/examples/tool_call_example http://localhost:11434 "" qwen3:8b /api/chat

# OpenAI
./build/examples/tool_call_example https://api.openai.com sk-your-key gpt-4o
```

## Test

```bash
ctest --test-dir build
```

## Project structure

```
core/        Agent framework: controller, context strategy, policy, session
services/    Vendor implementations: LLM, ASR, TTS, tools, memory
             Layout: services/<module>/<vendor>/
io/          IoDevice abstraction + audio device interfaces
runtime/     AgentRuntime (device bus), CoreDevice, RouteTable
examples/    Runnable examples
tests/       Unit and property-based tests
```

## Architecture

The runtime is a device bus. Every component — including the agent session — is an `IoDevice`. Data flows as typed `DataFrame` packets routed by a `RouteTable`.

```
User input
    │  text/plain
    ▼
CoreDevice (AgentSession adapter)
    │  text/plain
    ▼
app_output sink → OutputCallback
```

Audio and voice components (VAD, ASR, TTS) are registered as `IoDevice` instances and connected via routes. DMA routes (`requires_control_plane = false`) bypass the LLM loop for low-latency audio streaming.

The agent core is modeled after an OS: controller as state machine, LLM as CPU, context strategy as memory manager, policy layer as permission boundary.

## Cross-platform

| Platform      | Audio backend      | UI      |
|---------------|--------------------|---------|
| macOS / Linux | PortAudio          | Flutter |
| Windows       | PortAudio / WASAPI | Flutter |
| Android       | Oboe               | Flutter |
| iOS           | CoreAudio          | Flutter |

C++ core is shared across all platforms. Platform-specific code lives behind abstract interfaces.
