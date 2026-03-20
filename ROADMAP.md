# Roadmap

## Phase 1 — Agent Framework (Done)

- [x] Controller: state machine with planning, routing, retry, stop conditions
- [x] Context strategy: prompt window management, token budget
- [x] Policy layer: permission checks and audit before every tool call
- [x] Session: full agent loop (observe → context → LLM → route → act → repeat)
- [x] OpenAI-compatible LLM client: HTTP + SSE streaming
- [x] Tool dispatcher and registry
- [x] In-memory store, log audit sink

## Phase 2 — Runtime IO Redesign (Done)

- [x] `IoDevice` abstract interface: typed input/output ports, `DataFrame` packets
- [x] `RouteTable`: source→destination port routing, control plane + DMA paths
- [x] `CoreDevice`: `IoDevice` adapter wrapping `AgentSession`
- [x] `AgentRuntime`: device bus — zero data transformation, pure frame routing
- [x] Full test suite: unit + property-based tests for runtime, controller, context, policy
- [x] `InitLogger` idempotency fix

## Phase 3 — Services Restructure (Done)

- [x] Reorganize `services/` into `services/<module>/<vendor>` layout
- [x] `services/llm/openai/` → `shizuru_llm_openai`
- [x] `services/asr/baidu/` → `shizuru_asr_baidu`
- [x] `services/tts/baidu/` → `shizuru_tts_baidu`
- [x] `services/tts/elevenlabs/` → `shizuru_tts_elevenlabs`
- [x] `services/utils/baidu/` → `shizuru_baidu_utils` (shared token manager)

## Phase 4 — Voice Pipeline

Wire voice components as `IoDevice` instances into the runtime bus.

- [ ] VAD device: voice activity detection (WebRTC VAD or Silero)
- [ ] ASR device: wrap `BaiduAsrClient` / Whisper as an `IoDevice`
- [ ] TTS device: wrap `BaiduTtsClient` / ElevenLabs as an `IoDevice`
- [ ] Audio capture device: push/callback-driven recorder `IoDevice`
- [ ] Audio playout device: player `IoDevice`
- [ ] DMA routes: capture → VAD → ASR → CoreDevice (audio_in), CoreDevice → TTS → playout
- [ ] Control plane: interrupt and reroute commands from controller to voice devices

## Phase 5 — Platform Audio Backends

- [ ] Android: Oboe backend (`io/audio/audio_device/oboe/`)
- [ ] iOS: CoreAudio backend (`io/audio/audio_device/core_audio/`)
- [ ] Windows: WASAPI backend (`io/audio/audio_device/wasapi/`)

## Phase 6 — Flutter UI

- [ ] dart:ffi bridge: expose `AgentRuntime` as a C API, bind from Dart
- [ ] Conversation view: message history, input field, audio waveform indicator
- [ ] Debug panel: state machine status, token usage, tool call log, route table view
- [ ] Cross-platform Flutter app scaffolding (desktop + mobile)

## Phase 7 — Production Hardening

- [ ] Config loader: `RuntimeConfig` from JSON/YAML file
- [ ] Persistent `MemoryStore`: SQLite-backed, survives restarts
- [ ] Token counting: tiktoken or equivalent for accurate budget management
- [ ] Human-in-the-loop approval flow (`PolicyLayer::ResolveApproval`)
- [ ] Built-in tools: filesystem read/write, HTTP fetch, code runner
- [ ] CI: GitHub Actions on macOS, Linux, Windows
- [ ] Integration test: full voice pipeline end-to-end (capture → VAD → ASR → agent → TTS → playout)
