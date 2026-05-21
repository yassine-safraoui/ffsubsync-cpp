#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ffsubsync/subtitle_speech.h"
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/constants.h"
#include <vector>
#include <string>

static const char* fake_srt = R"(1
00:00:00,178 --> 00:00:01,416
Previously on...

2
00:00:01,828 --> 00:00:04,549
Oh hi, Mark.
)";

TEST_CASE("Subtitle speech extraction from fake SRT", "[speech]") {
    auto entries = ffsubsync::parse_srt(fake_srt);
    REQUIRE(entries.size() == 2);

    // Extract speech at 100 Hz (10 ms windows)
    auto speech = ffsubsync::extract_speech(entries, ffsubsync::SAMPLE_RATE);

    // Total duration ~4.549 s => ~455 frames at 100 Hz
    // We allow a small tolerance because rounding depends on implementation.
    REQUIRE(speech.size() >= 450);
    REQUIRE(speech.size() <= 460);

    // First subtitle: 178 ms -> 1416 ms  (length ~1238 ms => ~124 frames)
    // Second subtitle: 1828 ms -> 4549 ms (length ~2721 ms => ~272 frames)
    // Check that speech is marked roughly in the expected windows.
    // Frame i corresponds to time [i*10, (i+1)*10) ms.

    size_t frame_first_start  = 178  / 10; // 17
    size_t frame_first_end    = 1416 / 10; // 141
    size_t frame_second_start = 1828 / 10; // 182
    size_t frame_second_end   = 4549 / 10; // 454

    // Inside first window there should be at least some speech frames.
    bool first_has_speech = false;
    for (size_t i = frame_first_start; i <= frame_first_end && i < speech.size(); ++i) {
        if (speech[i] > 0.0f) {
            first_has_speech = true;
            break;
        }
    }
    REQUIRE(first_has_speech);

    // Inside second window there should be speech frames.
    bool second_has_speech = false;
    for (size_t i = frame_second_start; i <= frame_second_end && i < speech.size(); ++i) {
        if (speech[i] > 0.0f) {
            second_has_speech = true;
            break;
        }
    }
    REQUIRE(second_has_speech);

    // Before first subtitle starts, there should be no speech (metadata at beginning handled).
    bool pre_has_speech = false;
    for (size_t i = 0; i < frame_first_start && i < speech.size(); ++i) {
        if (speech[i] > 0.0f) {
            pre_has_speech = true;
            break;
        }
    }
    REQUIRE_FALSE(pre_has_speech);
}
