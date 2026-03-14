#pragma once

namespace shizuru::io {

// Ensures Pa_Initialize() is called exactly once.
// Safe to call multiple times; only the first call initializes.
void EnsurePaInitialized();

}  // namespace shizuru::io
