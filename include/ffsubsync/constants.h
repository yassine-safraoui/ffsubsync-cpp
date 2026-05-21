#pragma once
#include <array>
#include <string>

namespace ffsubsync {

// Sample rate for speech detection windows (Hz)
// 100 Hz = 10ms windows
inline constexpr int SAMPLE_RATE = 100;

// Audio frame rate for extraction (Hz)
inline constexpr int DEFAULT_FRAME_RATE = 48000;

// Label for non-speech frames (0.0 = definite non-speech)
inline constexpr float DEFAULT_NON_SPEECH_LABEL = 0.0f;

// Default input encoding detection
inline constexpr const char* DEFAULT_ENCODING = "infer";

// Maximum subtitle duration on screen (seconds)
inline constexpr int DEFAULT_MAX_SUBTITLE_SECONDS = 10;

// Start processing at this offset (seconds)
inline constexpr int DEFAULT_START_SECONDS = 0;

// Default scale factor for framerate
inline constexpr double DEFAULT_SCALE_FACTOR = 1.0;

// Default VAD strategy
inline constexpr const char* DEFAULT_VAD = "subs_then_webrtc";

// Maximum allowed offset for alignment (seconds)
inline constexpr int DEFAULT_MAX_OFFSET_SECONDS = 60;

// Additional offset to apply after sync (seconds)
inline constexpr int DEFAULT_APPLY_OFFSET_SECONDS = 0;

// Common framerate ratios to try for fixing mismatched framerates
inline constexpr std::array<double, 3> FRAMERATE_RATIOS = {
    24.0 / 23.976,
    25.0 / 23.976,
    25.0 / 24.0
};

// Supported subtitle extensions
inline constexpr std::array<const char*, 4> SUBTITLE_EXTENSIONS = {
    "srt", "ass", "ssa", "sub"
};

} // namespace ffsubsync
