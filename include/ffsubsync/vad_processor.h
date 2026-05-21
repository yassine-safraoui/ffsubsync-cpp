#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ffsubsync/types.h"

namespace ffsubsync {

/**
 * @brief Wraps Sherpa ONNX Silero VAD to convert raw PCM audio into a binary speech array.
 *
 * Processes audio using the Silero VAD model via Sherpa ONNX.
 * Input must be mono float PCM at 16000 Hz (or 8000 Hz).
 * Outputs a binary speech vector at a configurable sample rate:
 *   1.0f = speech detected
 *   0.0f = no speech
 *
 * Supports both batch (process) and streaming (feed/flush/drain_segments) APIs.
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
     * Convenience wrapper around feed() + flush() + drain_segments() +
     * to_binary_vector().
     *
     * @param samples Float PCM samples (mono).
     * @param output_sample_rate Desired output sample rate in Hz (default 100).
     * @return Binary speech vector (1.0 = speech, 0.0 = non-speech).
     */
    std::vector<float> process(const std::vector<float>& samples, float output_sample_rate = 100.0f);

    /**
     * @brief Incremental feed. Can be called with any chunk size (even 1 sample).
     */
    void feed(const float* samples, size_t count);

    /**
     * @brief Signal end of stream. Required to finalize the last in-progress segment.
     */
    void flush();

    /**
     * @brief Reset all state for a new audio stream.
     */
    void reset();

    /**
     * @brief Pop all completed segments from Sherpa's internal queue.
     *
     * Call after flush() (or periodically if doing real-time streaming).
     */
    std::vector<SpeechSegment> drain_segments();

    /**
     * @brief Convert collected segments to binary speech vector.
     *
     * @param segments Speech segments from drain_segments().
     * @param audio_sample_rate Sample rate of the audio (e.g. 16000).
     * @param output_sample_rate Desired output vector sample rate in Hz (default 100).
     * @param total_duration_seconds Total duration of the audio (includes trailing silence).
     * @return Binary speech vector (1.0 = speech, 0.0 = non-speech).
     */
    static std::vector<float> to_binary_vector(
        const std::vector<SpeechSegment>& segments,
        int audio_sample_rate,
        float output_sample_rate,
        double total_duration_seconds);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace ffsubsync
