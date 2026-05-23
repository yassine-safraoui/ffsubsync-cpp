#include "ffsubsync/vad_processor.h"
#include "ffsubsync/logging.h"

#include "sherpa-onnx/c-api/cxx-api.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ffsubsync {

struct VADProcessor::Impl {
    std::unique_ptr<sherpa_onnx::cxx::VoiceActivityDetector> vad;
    int sample_rate = 16000;
    std::string model_path;
    bool flushed = false;
};

VADProcessor::VADProcessor(const std::string& model_path, int sample_rate)
    : impl_(new Impl{}) {
    if (sample_rate != 16000 && sample_rate != 8000) {
        throw std::invalid_argument(
            "VADProcessor: Sherpa ONNX Silero VAD supports only 8000 or 16000 Hz");
    }

    sherpa_onnx::cxx::VadModelConfig config;
    config.silero_vad.model = model_path;
    config.silero_vad.threshold = 0.5f;
    config.silero_vad.min_silence_duration = 0.5f;
    config.silero_vad.min_speech_duration = 0.25f;
    config.silero_vad.max_speech_duration = 20.0f;
    config.silero_vad.window_size = 512;
    config.sample_rate = sample_rate;
    config.num_threads = 1;
    config.provider = "cpu";

    // 60 seconds = ~3.8 MB. Sufficient for film/TV without excessive memory.
    float buffer_size_in_seconds = 60.0f;

    impl_->vad = std::make_unique<sherpa_onnx::cxx::VoiceActivityDetector>(
        sherpa_onnx::cxx::VoiceActivityDetector::Create(
            config, buffer_size_in_seconds));
    if (!impl_->vad || !impl_->vad->Get()) {
        spdlog::error("VADProcessor: failed to create Sherpa ONNX VAD instance with model: {}", model_path);
        throw std::runtime_error(
            "VADProcessor: failed to create Sherpa ONNX VAD instance");
    }

    impl_->sample_rate = sample_rate;
    impl_->model_path = model_path;
    spdlog::info("VADProcessor: initialized with sample_rate={}, model={}", sample_rate, model_path);
}

VADProcessor::~VADProcessor() {
    delete impl_;
}

VADProcessor::VADProcessor(VADProcessor&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

VADProcessor& VADProcessor::operator=(VADProcessor&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

void VADProcessor::feed(const float* samples, size_t count) {
    if (!impl_) {
        throw std::runtime_error("VADProcessor: not initialized");
    }
    if (!samples || count == 0) {
        return;
    }

    // Feed in chunks. Window size must match the config (512).
    constexpr int32_t kWindowSize = 512;
    size_t i = 0;
    while (i + kWindowSize <= count) {
        impl_->vad->AcceptWaveform(samples + i, kWindowSize);
        i += kWindowSize;
    }
    // Tail chunk (may be smaller than window size).
    if (i < count) {
        impl_->vad->AcceptWaveform(
            samples + i,
            static_cast<int32_t>(count - i));
    }
}

void VADProcessor::flush() {
    if (!impl_) {
        throw std::runtime_error("VADProcessor: not initialized");
    }
    impl_->vad->Flush();
    impl_->flushed = true;
}

std::vector<SpeechSegment> VADProcessor::drain_segments() {
    if (!impl_) {
        throw std::runtime_error("VADProcessor: not initialized");
    }

    std::vector<SpeechSegment> segments;
    while (!impl_->vad->IsEmpty()) {
        auto segment = impl_->vad->Front();
        SpeechSegment seg;
        seg.start_sample = static_cast<int>(segment.start);
        seg.end_sample = static_cast<int>(segment.start + segment.samples.size());
        segments.push_back(seg);
        impl_->vad->Pop();
    }
    return segments;
}

std::vector<float> VADProcessor::to_binary_vector(
    const std::vector<SpeechSegment>& segments,
    int audio_sample_rate,
    float output_sample_rate,
    double total_duration_seconds) {

    if (output_sample_rate <= 0.0f) {
        throw std::invalid_argument(
            "VADProcessor: output_sample_rate must be positive");
    }
    if (audio_sample_rate <= 0) {
        throw std::invalid_argument(
            "VADProcessor: audio_sample_rate must be positive");
    }

    int num_output_samples =
        static_cast<int>(std::ceil(total_duration_seconds * output_sample_rate));
    if (num_output_samples <= 0) {
        return {};
    }

    std::vector<float> speech_vector(num_output_samples, 0.0f);

    for (const auto& seg : segments) {
        double start_time = static_cast<double>(seg.start_sample) / audio_sample_rate;
        double end_time = static_cast<double>(seg.end_sample) / audio_sample_rate;

        int start_idx = static_cast<int>(std::floor(start_time * output_sample_rate));
        int end_idx = static_cast<int>(std::ceil(end_time * output_sample_rate));

        if (start_idx < 0) start_idx = 0;
        if (end_idx > num_output_samples) end_idx = num_output_samples;

        for (int idx = start_idx; idx < end_idx; ++idx) {
            speech_vector[idx] = 1.0f;
        }
    }

    return speech_vector;
}

std::vector<float> VADProcessor::process(const std::vector<float>& samples,
                                          float output_sample_rate) {
    if (!impl_) {
        throw std::runtime_error("VADProcessor: not initialized");
    }
    if (output_sample_rate <= 0.0f) {
        throw std::invalid_argument(
            "VADProcessor: output_sample_rate must be positive");
    }

    // Clear any state from a previous run.
    reset();

    // Feed all samples.
    feed(samples.data(), samples.size());

    // Flush and drain.
    flush();
    auto segments = drain_segments();

    // Convert to binary vector.
    double total_duration =
        static_cast<double>(samples.size()) / impl_->sample_rate;
    return to_binary_vector(segments, impl_->sample_rate, output_sample_rate, total_duration);
}

void VADProcessor::reset() {
    if (impl_) {
        impl_->vad->Reset();
        impl_->flushed = false;
    }
}

} // namespace ffsubsync
