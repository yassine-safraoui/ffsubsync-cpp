#include "ffsubsync/constants.h"
#include "ffsubsync/subtitle_transformer.h"
#include "ffsubsync/types.h"
#include <chrono>
#include <vector>

namespace ffsubsync {

std::vector<SubtitleEntry> shift_subtitles(const std::vector<SubtitleEntry>& subs,
                                           std::chrono::milliseconds offset) {
    std::vector<SubtitleEntry> result;
    result.reserve(subs.size());

    for (const auto& sub : subs) {
        SubtitleEntry shifted = sub;
        shifted.start = sub.start + offset;
        shifted.end = sub.end + offset;
        result.push_back(std::move(shifted));
    }

    return result;
}

std::vector<SubtitleEntry> scale_subtitles(const std::vector<SubtitleEntry>& subs, double factor) {
    std::vector<SubtitleEntry> result;
    result.reserve(subs.size());

    for (const auto& sub : subs) {
        SubtitleEntry scaled = sub;

        double start_seconds = std::chrono::duration<double>(sub.start).count();
        double end_seconds = std::chrono::duration<double>(sub.end).count();

        scaled.start = std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(
            std::round(start_seconds * factor * 1000.0)));
        scaled.end = std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(
            std::round(end_seconds * factor * 1000.0)));

        result.push_back(std::move(scaled));
    }

    return result;
}

} // namespace ffsubsync
