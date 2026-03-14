#include "audio_device/port_audio/pa_init.h"
#include <portaudio.h>
#include <stdexcept>
#include <string>

namespace shizuru::io {

void EnsurePaInitialized() {
  static bool initialized = [] {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
      throw std::runtime_error(
          std::string("PortAudio init failed: ") + Pa_GetErrorText(err));
    }
    std::atexit([] { Pa_Terminate(); });
    return true;
  }();
  (void)initialized;
}

}  // namespace shizuru::io
