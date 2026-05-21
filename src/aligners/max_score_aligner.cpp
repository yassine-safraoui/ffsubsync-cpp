#include "ffsubsync/aligner.h"
#include "ffsubsync/types.h"

#include <cmath>
#include <limits>
#include <optional>

namespace ffsubsync {

MaxScoreAligner::MaxScoreAligner(const FFTAligner& base_aligner,
                                 int sample_rate,
                                 std::optional<double> max_offset_seconds)
    : base_aligner_(base_aligner),
      sample_rate_(sample_rate),
      max_offset_samples_(max_offset_seconds.has_value()
                          ? std::optional<int>(static_cast<int>(*max_offset_seconds * sample_rate))
                          : std::nullopt) {}

AlignmentResult MaxScoreAligner::find_best_alignment(
    const std::vector<float>& reference_speech,
    const std::vector<Candidate>& candidates) {

    AlignmentResult best_result;
    best_result.score = -std::numeric_limits<double>::infinity();
    best_result.success = false;

    for (const auto& candidate : candidates) {
        FFTAligner::Result align_result =
            base_aligner_.align(reference_speech, candidate.speech_frames);

        // Apply MaxScoreAligner's own offset constraint, if any.
        if (max_offset_samples_.has_value()) {
            if (std::abs(align_result.offset_samples) > *max_offset_samples_) {
                continue;
            }
        }

        // Keep the candidate with the highest correlation score.
        if (align_result.score > best_result.score) {
            best_result.score = align_result.score;
            best_result.offset_samples = align_result.offset_samples;
            best_result.framerate_ratio = candidate.framerate_ratio;
            best_result.success = true;
        }
    }

    if (!best_result.success) {
        best_result.error_message =
            "Failed to find a valid alignment within the allowed offset range";
    }

    return best_result;
}

} // namespace ffsubsync
