#pragma once
#include "ffsubsync/types.h"
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ffsubsync {

// SRT Parser
class SRTParser {
public:
    std::vector<SubtitleEntry> parse(const std::filesystem::path& path);
    std::vector<SubtitleEntry> parse_string(const std::string& content);

private:
    static std::vector<std::string> split_blocks(const std::string& content);
    static std::optional<SubtitleEntry> parse_block(const std::string& block);
    static bool looks_like_timestamp(const std::string& line);
    static std::optional<std::pair<std::chrono::milliseconds, std::chrono::milliseconds>>
    parse_timestamp_line(const std::string& line);
    static std::chrono::milliseconds parse_time_component(const std::string& hours_str,
                                                           const std::string& minutes_str,
                                                           const std::string& seconds_str,
                                                           const std::string& millis_str);
};

// Free function wrappers for convenience
std::vector<SubtitleEntry> parse_srt(const std::string& content);
std::vector<SubtitleEntry> parse_srt_file(const std::filesystem::path& path);

// SRT Writer
class SRTWriter {
public:
    static void write(const std::vector<SubtitleEntry>& entries, const std::filesystem::path& path);
    static std::string to_string(const std::vector<SubtitleEntry>& entries);

private:
    static std::string format_timestamp(std::chrono::milliseconds ms);
};

// Free function wrapper
std::string write_srt(const std::vector<SubtitleEntry>& entries);

} // namespace ffsubsync
