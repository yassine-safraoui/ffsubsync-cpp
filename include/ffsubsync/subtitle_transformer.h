#pragma once
#include "ffsubsync/types.h"
#include <vector>
#include <chrono>

namespace ffsubsync {

std::vector<SubtitleEntry> shift_subtitles(const std::vector<SubtitleEntry>& subs,
                                           std::chrono::milliseconds offset);

std::vector<SubtitleEntry> scale_subtitles(const std::vector<SubtitleEntry>& subs, double factor);

} // namespace ffsubsync
