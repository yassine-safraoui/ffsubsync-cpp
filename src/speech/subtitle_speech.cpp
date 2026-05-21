#include "ffsubsync/constants.h"
#include "ffsubsync/subtitle_speech.h"
#include "ffsubsync/types.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffsubsync {

SubtitleSpeechTransformer::SubtitleSpeechTransformer(int sample_rate,
                                                     double start_seconds,
                                                     double framerate_ratio)
    : sample_rate_(sample_rate)
    , start_seconds_(start_seconds)
    , framerate_ratio_(framerate_ratio) {}

std::vector<float> SubtitleSpeechTransformer::extract_speech(const std::vector<SubtitleEntry>& subtitles) {
    if (subtitles.empty()) {
        return {};
    }

    double max_time = 0.0;
    for (const auto& sub : subtitles) {
        double end_seconds = std::chrono::duration<double>(sub.end).count();
        if (end_seconds > max_time) {
            max_time = end_seconds;
        }
    }

    int num_samples = static_cast<int>(max_time * sample_rate_) + 2;
    std::vector<float> samples(num_samples, 0.0f);

    float speech_value = static_cast<float>(std::min(1.0 / framerate_ratio_, 1.0));

    for (size_t i = 0; i < subtitles.size(); ++i) {
        const auto& sub = subtitles[i];
        bool is_boundary = (i == 0) || (i + 1 == subtitles.size());
        if (is_metadata(sub.content, is_boundary)) {
            continue;
        }

        double sub_start = std::chrono::duration<double>(sub.start).count();
        double sub_end = std::chrono::duration<double>(sub.end).count();
        double duration = sub_end - sub_start;

        int start = static_cast<int>(std::round((sub_start - start_seconds_) * sample_rate_));
        int end = start + static_cast<int>(std::round(duration * sample_rate_));

        if (start < 0) {
            start = 0;
        }
        if (end > num_samples) {
            end = num_samples;
        }
        if (start < end) {
            std::fill(samples.begin() + start, samples.begin() + end, speech_value);
        }
    }

    return samples;
}

bool SubtitleSpeechTransformer::is_metadata(const std::string& content, bool is_beginning_or_end) {
    static const std::unordered_map<char, char> paired_nester = {
        {'(', ')'},
        {'{', '}'},
        {'[', ']'}
    };

    std::string trimmed;
    trimmed.reserve(content.size());
    for (char c : content) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            trimmed.push_back(c);
        }
    }

    if (trimmed.empty()) {
        return true;
    }

    auto it = paired_nester.find(trimmed.front());
    if (it != paired_nester.end() && trimmed.back() == it->second) {
        return true;
    }

    if (is_beginning_or_end) {
        std::string lowered;
        lowered.reserve(trimmed.size());
        for (char c : trimmed) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lowered.find("english") != std::string::npos) {
            return true;
        }
        if (lowered.find(" - ") != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::vector<float> extract_speech(const std::vector<SubtitleEntry>& entries, int sample_rate) {
    SubtitleSpeechTransformer transformer(sample_rate);
    return transformer.extract_speech(entries);
}

} // namespace ffsubsync
