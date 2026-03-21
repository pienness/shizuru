// Voice agent example: Microphone → VAD → ASR → AgentRuntime (LLM) → TTS → Speaker
//
// Pipeline topology:
//
//   [AudioCaptureDevice] audio_out ──► [EnergyVadDevice] audio_in
//   [EnergyVadDevice]    audio_out ──► [BaiduAsrDevice]  audio_in
//   [EnergyVadDevice]    vad_out   ──► [VadEventDevice]  vad_in
//                                          └── on speech_end → asr.Flush()
//   [BaiduAsrDevice]     text_out  ──► [core]            text_in
//   [core]               text_out  ──► app_output (AgentRuntime built-in sink)
//   [BaiduTtsDevice]     audio_out ──► [AudioPlayoutDevice] audio_in
//
// The TTS device is driven by the AgentRuntime output callback: when the LLM
// produces a final text response, it is fed directly into BaiduTtsDevice.
//
// All audio routes use requires_control_plane = false (DMA path).
// The LLM/controller path is handled internally by AgentRuntime.
//
// Usage:
//   export BAIDU_API_KEY=...
//   export BAIDU_SECRET_KEY=...
//   export OPENAI_API_KEY=...
//   ./voice_agent [--base-url <url>] [--model <model>] [--debug]
//
// Speak naturally — VAD handles segmentation. Ctrl+C to quit.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>
#include "async_logger.h"
#include "io/data_frame.h"
#include "io/asr/baidu/baidu_asr_device.h"
#include "io/tts/baidu/baidu_tts_device.h"
#include "io/vad/energy_vad_device.h"
#include "io/vad/vad_event_device.h"
#include "audio/audio_capture_device.h"
#include "audio/audio_playout_device.h"
#include "audio_device/port_audio/pa_player.h"
#include "audio_device/port_audio/pa_recorder.h"
#include "utils/baidu/baidu_config.h"
#include "runtime/agent_runtime.h"
#include "runtime/route_table.h"
#include "io/tool_registry.h"
#include "llm/config.h"

using namespace shizuru;

int main(int argc, char* argv[]) {
  // ── CLI args ──────────────────────────────────────────────────────────────
  bool debug_mode = false;
  std::string base_url = "https://dashscope.aliyuncs.com";
  std::string model    = "qwen3-coder-next";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--debug") {
      debug_mode = true;
    } else if (arg == "--base-url" && i + 1 < argc) {
      base_url = argv[++i];
    } else if (arg == "--model" && i + 1 < argc) {
      model = argv[++i];
    }
  }

  // ── Environment ───────────────────────────────────────────────────────────
  const char* baidu_ak  = std::getenv("BAIDU_API_KEY");
  const char* baidu_sk  = std::getenv("BAIDU_SECRET_KEY");
  const char* openai_key = std::getenv("OPENAI_API_KEY");

  if (baidu_ak == nullptr || baidu_sk == nullptr) {
    std::fprintf(stderr,
                 "Error: set BAIDU_API_KEY and BAIDU_SECRET_KEY env vars.\n");
    return 1;
  }
  if (openai_key == nullptr) {
    std::fprintf(stderr, "Error: set OPENAI_API_KEY env var.\n");
    return 1;
  }

  // ── Logger ────────────────────────────────────────────────────────────────
  core::LoggerConfig log_cfg;
  log_cfg.level = debug_mode ? spdlog::level::debug : spdlog::level::info;
  core::InitLogger(log_cfg);

  // ── Baidu config ──────────────────────────────────────────────────────────
  services::BaiduConfig baidu_cfg;
  baidu_cfg.api_key    = baidu_ak;
  baidu_cfg.secret_key = baidu_sk;
  baidu_cfg.aue        = 5;      // PCM 16kHz output from TTS
  baidu_cfg.per        = 0;      // female voice
  baidu_cfg.asr_format = "pcm";

  // ── Audio config ──────────────────────────────────────────────────────────
  constexpr int    kRate = 16000;
  constexpr size_t kCh   = 1;
  constexpr size_t kFpb  = 320;  // 20ms at 16kHz

  io::RecorderConfig rec_cfg;
  rec_cfg.sample_rate             = kRate;
  rec_cfg.channel_count           = kCh;
  rec_cfg.frames_per_buffer       = kFpb;
  rec_cfg.buffer_capacity_samples = static_cast<size_t>(kRate) * 5;

  io::PlayerConfig play_cfg;
  play_cfg.sample_rate             = kRate;
  play_cfg.channel_count           = kCh;
  play_cfg.frames_per_buffer       = kFpb;
  play_cfg.buffer_capacity_samples = static_cast<size_t>(kRate) * 10;

  // ── Shared token manager ──────────────────────────────────────────────────
  auto token_mgr = std::make_shared<services::BaiduTokenManager>(baidu_cfg);
  token_mgr->GetToken();  // pre-warm

  // ── Voice devices (owned by AgentRuntime via RegisterDevice) ─────────────
  auto capture = std::make_unique<io::AudioCaptureDevice>(
      std::make_unique<io::PaRecorder>(rec_cfg));
  auto playout = std::make_unique<io::AudioPlayoutDevice>(
      std::make_unique<io::PaPlayer>(play_cfg));

  io::EnergyVadConfig vad_cfg;
  vad_cfg.energy_threshold        = 600.0F;
  vad_cfg.speech_onset_frames     = 3;   // ~60ms
  vad_cfg.silence_hangover_frames = 20;  // ~400ms
  vad_cfg.pre_roll_frames         = 3;
  auto vad = std::make_unique<io::EnergyVadDevice>(vad_cfg);

  auto asr = std::make_unique<io::BaiduAsrDevice>(baidu_cfg, token_mgr);
  auto tts = std::make_unique<io::BaiduTtsDevice>(baidu_cfg, token_mgr);

  // Keep raw pointers before moving ownership into the runtime.
  io::BaiduAsrDevice* asr_ptr = asr.get();
  io::BaiduTtsDevice* tts_ptr = tts.get();

  // VadEventDevice: triggers asr.Flush() on speech_end.
  auto asr_flush = std::make_unique<io::VadEventDevice>(
      [asr_ptr](const std::string& /*event*/) { asr_ptr->Flush(); });

  // ── AgentRuntime config ───────────────────────────────────────────────────
  runtime::RuntimeConfig rt_cfg;
  rt_cfg.logger                        = log_cfg;
  rt_cfg.llm.base_url                  = base_url;
  rt_cfg.llm.api_path                  = "/compatible-mode/v1/chat/completions";
  rt_cfg.llm.api_key                   = openai_key;
  rt_cfg.llm.model                     = model;
  rt_cfg.llm.connect_timeout           = std::chrono::seconds(10);
  rt_cfg.llm.read_timeout              = std::chrono::seconds(60);
  rt_cfg.context.default_system_instruction =
      "You are a helpful voice assistant. Keep responses concise and natural "
      "for speech. Avoid markdown formatting.";
  rt_cfg.controller.max_turns          = 100;

  services::ToolRegistry tools;  // no tools for this example

  runtime::AgentRuntime runtime(rt_cfg, tools);

  // ── Register voice devices ────────────────────────────────────────────────
  runtime.RegisterDevice(std::move(capture));
  runtime.RegisterDevice(std::move(vad));
  runtime.RegisterDevice(std::move(asr_flush));
  runtime.RegisterDevice(std::move(asr));
  runtime.RegisterDevice(std::move(tts));
  runtime.RegisterDevice(std::move(playout));

  // ── Routes (all DMA — requires_control_plane = false) ────────────────────
  constexpr runtime::RouteOptions kDma{.requires_control_plane = false};

  // capture → vad
  runtime.AddRoute({"audio_capture", "audio_out"},
                   {"vad",           io::EnergyVadDevice::kAudioIn}, kDma);

  // vad audio_out (speech frames) → asr
  runtime.AddRoute({"vad",       io::EnergyVadDevice::kAudioOut},
                   {"baidu_asr", "audio_in"}, kDma);

  // vad vad_out (events) → asr_flush (triggers Flush on speech_end)
  runtime.AddRoute({"vad",       io::EnergyVadDevice::kVadOut},
                   {"vad_event", io::VadEventDevice::kVadIn}, kDma);

  // asr text_out → core text_in (LLM reasoning)
  runtime.AddRoute({"baidu_asr", "text_out"},
                   {"core",      "text_in"}, kDma);

  // tts audio_out → playout
  runtime.AddRoute({"baidu_tts",    "audio_out"},
                   {"audio_playout", "audio_in"}, kDma);

  // ── Output callback: LLM response → TTS ──────────────────────────────────
  runtime.OnOutput([tts_ptr](const runtime::RuntimeOutput& output) {
    std::printf("[agent] %s\n", output.text.c_str());
    std::fflush(stdout);

    // Feed LLM text into TTS for voice playback.
    io::DataFrame frame;
    frame.type    = "text/plain";
    frame.payload = std::vector<uint8_t>(output.text.begin(), output.text.end());
    frame.source_device = "app_output";
    frame.source_port   = "text_out";
    frame.timestamp     = std::chrono::steady_clock::now();
    tts_ptr->OnInput("text_in", std::move(frame));
  });

  // ── Start ─────────────────────────────────────────────────────────────────
  runtime.StartSession();

  std::printf("=== Voice Agent (VAD + Baidu ASR + %s LLM + Baidu TTS) ===\n",
              model.c_str());
  std::printf("Log level : %s\n",
              debug_mode ? "debug (all frames)" : "info (events + text)");
  std::printf("Speak naturally — VAD detects speech automatically.\n");
  std::printf("Ctrl+C to quit.\n\n");

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  runtime.Shutdown();
  return 0;
}
