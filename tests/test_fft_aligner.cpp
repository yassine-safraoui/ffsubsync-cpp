#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ffsubsync/aligner.h"
#include <vector>

TEST_CASE("FFTAligner simple offset -1", "[aligner]") {
    // Python test: fit_transform(s2="11001", s1="111001") => offset=-1
    // C++ align(ref=s2, sub=s1)
    ffsubsync::FFTAligner aligner;
    std::vector<float> ref = {1.0f, 1.0f, 0.0f, 0.0f, 1.0f};        // s2
    std::vector<float> sub = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f};  // s1
    auto result = aligner.align(ref, sub);
    REQUIRE(result.offset_samples == -1);
}

TEST_CASE("FFTAligner simple offset 0", "[aligner]") {
    ffsubsync::FFTAligner aligner;
    std::vector<float> ref = {1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<float> sub = {1.0f, 0.0f, 0.0f, 1.0f};
    auto result = aligner.align(ref, sub);
    REQUIRE(result.offset_samples == 0);
}

TEST_CASE("FFTAligner simple offset +1", "[aligner]") {
    // Python test: fit_transform(s2="01001", s1="10010") => offset=1
    // C++ align(ref=s2, sub=s1)
    ffsubsync::FFTAligner aligner;
    std::vector<float> ref = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};  // s2
    std::vector<float> sub = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f};  // s1
    auto result = aligner.align(ref, sub);
    REQUIRE(result.offset_samples == 1);
}

TEST_CASE("FFTAligner max_offset clips large offsets", "[aligner]") {
    std::vector<float> ref(64, 0.0f);
    for (int i = 20; i < 30; ++i) ref[i] = 1.0f;

    std::vector<float> sub(64, 0.0f);
    for (int i = 0; i < 10; ++i) sub[i] = 1.0f;

    SECTION("No max_offset — offset reflects true misalignment") {
        ffsubsync::FFTAligner aligner;
        auto result = aligner.align(ref, sub);
        REQUIRE(result.offset_samples == 20);
    }

    SECTION("max_offset = 10 — result is clipped to -9") {
        ffsubsync::FFTAligner aligner(10);
        auto result = aligner.align(ref, sub);
        REQUIRE(result.offset_samples == -9);
    }
}
