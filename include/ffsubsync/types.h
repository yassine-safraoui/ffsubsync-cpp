#pragma once
#include "ffsubsync/constants.h"
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <filesystem>
#include <functional>

namespace ffsubsync {

using TimePoint = std::chrono::milliseconds;

struct SubtitleEntry {
    TimePoint start{0};
    TimePoint end{0};
    std::string content;

    struct SRTData { int index{}; };
    struct ASSData { std::string style; std::string name; int layer{}; };
    struct VTTData { std::string cue_settings; };

    std::variant<std::monostate, SRTData, ASSData, VTTData> format_data;
};

struct SpeechSegment {
    int start_sample = 0;   // sample index at audio_sample_rate
    int end_sample = 0;     // exclusive
};

struct AlignmentResult {
    double score = 0.0;
    int offset_samples = 0;
    double framerate_ratio = 1.0;
    bool success = false;
    std::optional<std::string> error_message;
};

struct SyncOptions {
    std::filesystem::path reference_path;
    std::optional<std::string> reference_stream;

    std::vector<std::filesystem::path> input_subtitles;
    std::optional<std::filesystem::path> output_path;
    bool overwrite_input = false;

    int sample_rate = SAMPLE_RATE;
    int audio_frame_rate = DEFAULT_FRAME_RATE;
    double max_offset_seconds = DEFAULT_MAX_OFFSET_SECONDS;
    double start_seconds = DEFAULT_START_SECONDS;
    double apply_offset_seconds = DEFAULT_APPLY_OFFSET_SECONDS;
    bool fix_framerate = true;
    bool use_gss = false;

    enum class VADType { WebRTC, Auditok, Silero };
    VADType vad = VADType::WebRTC;
    float non_speech_label = DEFAULT_NON_SPEECH_LABEL;

    std::string input_encoding = DEFAULT_ENCODING;
    std::string output_encoding = "utf-8";
    double max_subtitle_seconds = DEFAULT_MAX_SUBTITLE_SECONDS;
    bool strict_parsing = false;

    bool merge_with_reference = false;
    bool serialize_speech = false;
    std::optional<double> suppress_output_threshold;

    using ProgressCallback = std::function<void(const std::string& stage, double fraction)>;
    using LogCallback = std::function<void(const std::string& message)>;

    ProgressCallback on_progress;
    LogCallback on_log;
};

class FailedToFindAlignmentException : public std::runtime_error {
public:
    explicit FailedToFindAlignmentException(const std::string& msg)
        : std::runtime_error(msg) {}
};

} // namespace ffsubsync
