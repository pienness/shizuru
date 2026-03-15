# Audio loopback example (requires PortAudio hardware)
add_executable(audio_loopback audio_loopback.cpp)
target_link_libraries(audio_loopback PRIVATE shizuru_audio)

# Tool call example (self-contained with mock LLM server)
add_executable(tool_call_example tool_call.cpp)
target_link_libraries(tool_call_example
  PRIVATE shizuru_runtime
  PRIVATE httplib::httplib
)
