#include <catch2/catch_test_macros.hpp>
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/types.h"
#include <vector>
#include <string>

static const char* sample_srt = R"(1
00:00:01,000 --> 00:00:03,500
First line.

2
00:00:04,000 --> 00:00:06,000
Second line.
More text here.

3
00:00:07,000 --> 00:00:09,000
Third line.
)";

TEST_CASE("SRT parser round-trip", "[parser]") {
    auto entries = ffsubsync::parse_srt(sample_srt);
    REQUIRE(entries.size() == 3);

    SECTION("Parsed indices and timings are correct") {
        // Entry 1
        REQUIRE(std::get<ffsubsync::SubtitleEntry::SRTData>(entries[0].format_data).index == 1);
        REQUIRE(entries[0].start.count() == 1000);
        REQUIRE(entries[0].end.count()   == 3500);
        REQUIRE(entries[0].content == "First line.");

        // Entry 2
        REQUIRE(std::get<ffsubsync::SubtitleEntry::SRTData>(entries[1].format_data).index == 2);
        REQUIRE(entries[1].start.count() == 4000);
        REQUIRE(entries[1].end.count()   == 6000);
        REQUIRE(entries[1].content == "Second line.\nMore text here.");

        // Entry 3
        REQUIRE(std::get<ffsubsync::SubtitleEntry::SRTData>(entries[2].format_data).index == 3);
        REQUIRE(entries[2].start.count() == 7000);
        REQUIRE(entries[2].end.count()   == 9000);
        REQUIRE(entries[2].content == "Third line.");
    }

    SECTION("Writing back produces expected format") {
        std::string out = ffsubsync::write_srt(entries);

        // Basic sanity checks on the serialized output.
        REQUIRE(out.find("1\n00:00:01,000 --> 00:00:03,500\nFirst line.") != std::string::npos);
        REQUIRE(out.find("2\n00:00:04,000 --> 00:00:06,000\nSecond line.\nMore text here.") != std::string::npos);
        REQUIRE(out.find("3\n00:00:07,000 --> 00:00:09,000\nThird line.") != std::string::npos);

        // Round-trip parse should yield identical data.
        auto reparsed = ffsubsync::parse_srt(out);
        REQUIRE(reparsed.size() == 3);
        for (size_t i = 0; i < 3; ++i) {
            REQUIRE(reparsed[i].start == entries[i].start);
            REQUIRE(reparsed[i].end   == entries[i].end);
            REQUIRE(reparsed[i].content == entries[i].content);
        }
    }
}
