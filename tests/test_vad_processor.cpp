#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ffsubsync/vad_processor.h"
#include <vector>
#include <cmath>
#include <string>
#include <filesystem>

static std::string get_model_path() {
    // Tests are run from the build directory; model is in source tree.
    std::filesystem::path src_dir = std::filesystem::path(__FILE__)
                                         .parent_path()
                                         .parent_path();
    return (src_dir / "models" / "silero_vad.onnx").string();
}

// Generate a longer sine-wave burst at the given frequency.
static std::vector<float> generate_sine_burst(int sample_rate,
                                               float frequency,
                                               int duration_ms) {
    const size_t num_samples = static_cast<size_t>(
        sample_rate * duration_ms / 1000);
    std::vector<float> samples(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        float val = std::sin(2.0f * static_cast<float>(M_PI) * frequency * t);
        samples[i] = val * 0.5f; // moderate amplitude
    }
    return samples;
}

// Generate silence.
static std::vector<float> generate_silence(int sample_rate, int duration_ms) {
    return std::vector<float>(
        static_cast<size_t>(sample_rate * duration_ms / 1000), 0.0f);
}

TEST_CASE("VADProcessor construction succeeds with valid model", "[vad]") {
    ffsubsync::VADProcessor vad(get_model_path(), 16000);
    REQUIRE(true);
}

TEST_CASE("VADProcessor construction fails with invalid model", "[vad]") {
    REQUIRE_THROWS(ffsubsync::VADProcessor("/nonexistent/model.onnx", 16000));
}

TEST_CASE("VADProcessor rejects unsupported sample rate", "[vad]") {
    REQUIRE_THROWS(ffsubsync::VADProcessor(get_model_path(), 48000));
}

TEST_CASE("VADProcessor process returns correctly sized vector", "[vad]") {
    ffsubsync::VADProcessor vad(get_model_path(), 16000);

    // 2 seconds of mixed signal
    auto speech = generate_sine_burst(16000, 440.0f, 1000);
    auto silence = generate_silence(16000, 1000);
    std::vector<float> combined;
    combined.insert(combined.end(), speech.begin(), speech.end());
    combined.insert(combined.end(), silence.begin(), silence.end());

    auto result = vad.process(combined, 100.0f);

    // 2 seconds @ 100 Hz = 200 samples
    REQUIRE(result.size() == 200);

    // All values must be exactly 0.0 or 1.0
    for (float v : result) {
        REQUIRE((v == 0.0f || v == 1.0f));
    }
}

TEST_CASE("VADProcessor process with different output rates", "[vad]") {
    ffsubsync::VADProcessor vad(get_model_path(), 16000);

    auto samples = generate_sine_burst(16000, 440.0f, 1000);

    auto r50 = vad.process(samples, 50.0f);
    REQUIRE(r50.size() == 50);

    auto r200 = vad.process(samples, 200.0f);
    REQUIRE(r200.size() == 200);
}

TEST_CASE("VADProcessor reset does not crash", "[vad]") {
    ffsubsync::VADProcessor vad(get_model_path(), 16000);
    auto samples = generate_sine_burst(16000, 440.0f, 500);
    vad.process(samples, 100.0f);
    vad.reset();
    REQUIRE(true);
}

TEST_CASE("VADProcessor silence returns empty or valid vector", "[vad]") {
    ffsubsync::VADProcessor vad(get_model_path(), 16000);

    auto silence = generate_silence(16000, 2000);
    auto result = vad.process(silence, 100.0f);

    REQUIRE(result.size() == 200);
    // Neural VAD may produce small false positives on pure silence;
    // we only assert structural validity here.
    for (float v : result) {
        REQUIRE((v == 0.0f || v == 1.0f));
    }
}
