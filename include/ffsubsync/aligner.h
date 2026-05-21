#pragma once
#include "ffsubsync/types.h"
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace ffsubsync {

class FFTAligner {
public:
    struct Result {
        int offset_samples = 0;
        double score = 0.0;
    };

    explicit FFTAligner(std::optional<int> max_offset_samples = std::nullopt);

    Result align(const std::vector<float>& refstring,
                 const std::vector<float>& substring);

private:
    std::optional<int> max_offset_samples_;

    void eliminate_extreme_offsets(std::vector<std::complex<float>>& convolve,
                                     size_t substring_len);
    void compute_argmax(const std::vector<std::complex<float>>& convolve,
                        size_t substring_len);

    int best_offset_ = 0;
    double best_score_ = 0.0;
};

class MaxScoreAligner {
public:
    MaxScoreAligner(const FFTAligner& base_aligner,
                    int sample_rate,
                    std::optional<double> max_offset_seconds = std::nullopt);

    struct Candidate {
        std::vector<float> speech_frames;
        double framerate_ratio;
    };

        AlignmentResult find_best_alignment(const std::vector<float>& reference_speech,
                                              const std::vector<Candidate>& candidates);

private:
    FFTAligner base_aligner_;
    int sample_rate_;
    std::optional<int> max_offset_samples_;
};

std::pair<double, double> golden_section_search(
    std::function<double(double, bool)> objective,
    double a, double b, double tol = 1e-4);

} // namespace ffsubsync
