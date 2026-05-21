#include <catch2/catch_test_macros.hpp>
#include "ffsubsync/ffmpeg_audio_decoder.h"
#include <filesystem>
#include <vector>

static std::filesystem::path get_test_video_path() {
    std::filesystem::path src_dir = std::filesystem::path(__FILE__)
                                         .parent_path()
                                         .parent_path();
    return src_dir / "test_data" / "test_sine.mp4";
}

TEST_CASE("FFmpegAudioDecoder default state", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder;
    REQUIRE(!decoder.is_open());
    REQUIRE(decoder.error_message().empty());
}

TEST_CASE("FFmpegAudioDecoder open valid file", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder;
    ffsubsync::FFmpegAudioDecoder::Config config;
    config.target_sample_rate = 16000;
    config.target_channels = 1;

    auto path = get_test_video_path();
    REQUIRE(std::filesystem::exists(path));

    bool ok = decoder.open(path, config);
    REQUIRE(ok);
    REQUIRE(decoder.is_open());
    REQUIRE(decoder.error_message().empty());

    const auto& info = decoder.info();
    REQUIRE(info.duration_seconds > 0.0);
    REQUIRE(info.original_sample_rate > 0);
    REQUIRE(info.original_channels > 0);
    REQUIRE(!info.codec_name.empty());
}

TEST_CASE("FFmpegAudioDecoder open invalid file", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder;
    ffsubsync::FFmpegAudioDecoder::Config config;

    bool ok = decoder.open("/nonexistent/file.mp4", config);
    REQUIRE(!ok);
    REQUIRE(!decoder.is_open());
    REQUIRE(!decoder.error_message().empty());
}

TEST_CASE("FFmpegAudioDecoder decode and resample", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder;
    ffsubsync::FFmpegAudioDecoder::Config config;
    config.target_sample_rate = 16000;
    config.target_channels = 1;

    auto path = get_test_video_path();
    REQUIRE(std::filesystem::exists(path));

    bool ok = decoder.open(path, config);
    REQUIRE(ok);

    ffsubsync::FFmpegAudioDecoder::AudioChunk chunk{};
    int total_samples = 0;
    int chunk_count = 0;

    while (decoder.next(chunk)) {
        REQUIRE(chunk.data != nullptr);
        REQUIRE(chunk.num_samples > 0);
        total_samples += chunk.num_samples;
        ++chunk_count;
    }

    REQUIRE(chunk_count > 0);
    REQUIRE(total_samples > 0);
    // 2 seconds @ 16kHz = ~32000 samples. Allow some margin for resampler delay.
    REQUIRE(total_samples >= 30000);
}

TEST_CASE("FFmpegAudioDecoder close resets state", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder;
    ffsubsync::FFmpegAudioDecoder::Config config;

    auto path = get_test_video_path();
    REQUIRE(std::filesystem::exists(path));

    bool ok = decoder.open(path, config);
    REQUIRE(ok);
    REQUIRE(decoder.is_open());

    decoder.close();
    REQUIRE(!decoder.is_open());
}

TEST_CASE("FFmpegAudioDecoder move semantics", "[ffmpeg]") {
    ffsubsync::FFmpegAudioDecoder decoder1;
    ffsubsync::FFmpegAudioDecoder::Config config;

    auto path = get_test_video_path();
    REQUIRE(std::filesystem::exists(path));

    bool ok = decoder1.open(path, config);
    REQUIRE(ok);

    ffsubsync::FFmpegAudioDecoder decoder2 = std::move(decoder1);
    REQUIRE(decoder2.is_open());
    REQUIRE(!decoder1.is_open());
}
