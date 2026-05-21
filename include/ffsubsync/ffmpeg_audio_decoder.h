#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ffsubsync {

/**
 * @brief Decodes audio from arbitrary media files using FFmpeg C API.
 *
 * Opens a video or audio file, finds the first audio stream, decodes packets,
 * resamples to the target format, and exposes frame-by-frame iteration.
 *
 * Desktop: open from filesystem path.
 * Android: open from POSIX file descriptor (passed via JNI).
 */
class FFmpegAudioDecoder {
public:
    struct Config {
        int target_sample_rate = 16000;
        int target_channels = 1;
        double start_seconds = 0.0;
    };

    struct StreamInfo {
        double duration_seconds = 0.0;
        int original_sample_rate = 0;
        int original_channels = 0;
        std::string codec_name;
    };

    struct AudioChunk {
        const float* data = nullptr;   // mono float PCM
        int num_samples = 0;           // sample count (mono)
    };

    FFmpegAudioDecoder();
    ~FFmpegAudioDecoder();

    // Disable copy, enable move
    FFmpegAudioDecoder(const FFmpegAudioDecoder&) = delete;
    FFmpegAudioDecoder& operator=(const FFmpegAudioDecoder&) = delete;
    FFmpegAudioDecoder(FFmpegAudioDecoder&&) noexcept;
    FFmpegAudioDecoder& operator=(FFmpegAudioDecoder&&) noexcept;

    // Open from filesystem path (desktop)
    bool open(const std::filesystem::path& path, const Config& config);

    // Open from POSIX file descriptor (Android JNI)
    bool open(int fd, const Config& config);

    void close();

    // Decode and resample the next chunk.
    // Returns true with chunk data while audio remains.
    // Returns false on EOF or fatal error (check error_message()).
    bool next(AudioChunk& chunk);

    const StreamInfo& info() const;
    bool is_open() const;
    std::string error_message() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ffsubsync
