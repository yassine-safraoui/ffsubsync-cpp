#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ffsubsync {

/**
 * @brief Wraps Sherpa ONNX Silero VAD to convert raw PCM audio into a binary speech array.
 *
 * Processes audio using the Silero VAD model via Sherpa ONNX.
 * Input must be mono float PCM at 16000 Hz (or 8000 Hz).
 * Outputs a binary speech vector at a configurable sample rate:
 *   1.0f = speech detected
 *   0.0f = no speech
 */
class VADProcessor {
public:
    explicit VADProcessor(const std::string& model_path, int sample_rate = 16000);
    ~VADProcessor();

    // Disable copy
    VADProcessor(const VADProcessor&) = delete;
    VADProcessor& operator=(const VADProcessor&) = delete;

    VADProcessor(VADProcessor&&) noexcept;
    VADProcessor& operator=(VADProcessor&&) noexcept;

    /**
     * @brief Process a contiguous block of float PCM mono samples.
     *
     * Feeds the entire buffer to the VAD, flushes, and converts the resulting
     * speech segments into a dense binary vector.
     *
     * @param samples Float PCM samples (mono).
     * @param output_sample_rate Desired output sample rate in Hz (default 100).
     * @return Binary speech vector (1.0 = speech, 0.0 = non-speech).
     */
    std::vector<float> process(const std::vector<float>& samples, float output_sample_rate = 100.0f);

    /**
     * @brief Reset the internal VAD state.
     */
    void reset();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace ffsubsync
