#pragma once
#include "ffsubsync/types.h"
#include <vector>

namespace ffsubsync {

class SubtitleSpeechTransformer {
public:
    explicit SubtitleSpeechTransformer(int sample_rate = 100,
                                       double start_seconds = 0.0,
                                       double framerate_ratio = 1.0);

    std::vector<float> extract_speech(const std::vector<SubtitleEntry>& subtitles);

    static bool is_metadata(const std::string& content, bool is_beginning_or_end);

private:
    int sample_rate_;
    double start_seconds_;
    double framerate_ratio_;
};

// Free function wrapper
std::vector<float> extract_speech(const std::vector<SubtitleEntry>& entries, int sample_rate = 100);

} // namespace ffsubsync
