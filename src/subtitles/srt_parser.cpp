#include "ffsubsync/constants.h"
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/types.h"
#include "ffsubsync/logging.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ffsubsync {

std::vector<SubtitleEntry> SRTParser::parse(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open SRT file: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_string(buffer.str());
}

std::vector<SubtitleEntry> SRTParser::parse_string(const std::string& content) {
    std::vector<SubtitleEntry> entries;
    std::vector<std::string> blocks = split_blocks(content);

    spdlog::debug("SRTParser::parse_string: {} blocks found", blocks.size());

    for (const auto& block : blocks) {
        if (block.empty()) {
            continue;
        }

        std::optional<SubtitleEntry> entry = parse_block(block);
        if (entry) {
            entries.push_back(*entry);
        }
    }

    spdlog::debug("SRTParser::parse_string: {} entries parsed", entries.size());

    return entries;
}

std::vector<std::string> SRTParser::split_blocks(const std::string& content) {
    std::vector<std::string> blocks;
    std::string current_block;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove carriage return for Windows line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (!current_block.empty()) {
                blocks.push_back(std::move(current_block));
                current_block.clear();
            }
        } else {
            if (!current_block.empty()) {
                current_block += '\n';
            }
            current_block += line;
        }
    }

    if (!current_block.empty()) {
        blocks.push_back(std::move(current_block));
    }

    return blocks;
}

std::optional<SubtitleEntry> SRTParser::parse_block(const std::string& block) {
    std::istringstream stream(block);
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }

    if (lines.empty()) {
        return std::nullopt;
    }

    size_t line_idx = 0;
    int index = 0;

    // Try to parse an optional index line
    if (!looks_like_timestamp(lines[line_idx])) {
        try {
            index = std::stoi(lines[line_idx]);
        } catch (...) {
            // Not a valid index, treat as part of content
            // But SRT blocks usually start with index or timestamp
        }
        ++line_idx;
    }

    if (line_idx >= lines.size()) {
        return std::nullopt;
    }

    // Parse timestamp line
    auto timestamp_result = parse_timestamp_line(lines[line_idx]);
    if (!timestamp_result) {
        return std::nullopt;
    }
    ++line_idx;

    // Remaining lines are content
    std::string content;
    for (; line_idx < lines.size(); ++line_idx) {
        if (!content.empty()) {
            content += '\n';
        }
        content += lines[line_idx];
    }

    SubtitleEntry entry;
    entry.start = timestamp_result->first;
    entry.end = timestamp_result->second;
    entry.content = std::move(content);
    entry.format_data = SubtitleEntry::SRTData{index};

    return entry;
}

bool SRTParser::looks_like_timestamp(const std::string& line) {
    // Quick check: timestamp lines contain "-->"
    return line.find("-->") != std::string::npos;
}

std::optional<std::pair<std::chrono::milliseconds, std::chrono::milliseconds>>
SRTParser::parse_timestamp_line(const std::string& line) {
    // Pattern: HH:MM:SS,mmm --> HH:MM:SS,mmm
    // Also accept . as milliseconds separator
    static const std::regex timestamp_regex(
        R"((\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3}))");

    std::smatch match;
    if (!std::regex_search(line, match, timestamp_regex)) {
        return std::nullopt;
    }

    auto start = parse_time_component(match[1].str(), match[2].str(), match[3].str(), match[4].str());
    auto end = parse_time_component(match[5].str(), match[6].str(), match[7].str(), match[8].str());

    return std::make_pair(start, end);
}

std::chrono::milliseconds SRTParser::parse_time_component(const std::string& hours_str,
                                                           const std::string& minutes_str,
                                                           const std::string& seconds_str,
                                                           const std::string& millis_str) {
    int hours = std::stoi(hours_str);
    int minutes = std::stoi(minutes_str);
    int seconds = std::stoi(seconds_str);
    int millis = std::stoi(millis_str);

    return std::chrono::milliseconds(
        hours * 3600000 + minutes * 60000 + seconds * 1000 + millis);
}

void SRTWriter::write(const std::vector<SubtitleEntry>& entries, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open SRT file for writing: " + path.string());
    }
    file << to_string(entries);
}

std::string SRTWriter::to_string(const std::vector<SubtitleEntry>& entries) {
    std::ostringstream oss;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        int index = static_cast<int>(i + 1);
        if (std::holds_alternative<SubtitleEntry::SRTData>(entry.format_data)) {
            index = std::get<SubtitleEntry::SRTData>(entry.format_data).index;
            if (index == 0) {
                index = static_cast<int>(i + 1);
            }
        }

        oss << index << '\n';
        oss << format_timestamp(entry.start) << " --> " << format_timestamp(entry.end) << '\n';
        oss << entry.content << '\n';
        if (i + 1 < entries.size()) {
            oss << '\n';
        }
    }
    return oss.str();
}

std::string SRTWriter::format_timestamp(std::chrono::milliseconds ms) {
    auto total_ms = ms.count();
    auto hours = total_ms / 3600000;
    total_ms %= 3600000;
    auto minutes = total_ms / 60000;
    total_ms %= 60000;
    auto seconds = total_ms / 1000;
    auto millis = total_ms % 1000;

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':'
        << std::setw(2) << seconds << ','
        << std::setw(3) << millis;
    return oss.str();
}

std::vector<SubtitleEntry> parse_srt(const std::string& content) {
    SRTParser parser;
    return parser.parse_string(content);
}

std::vector<SubtitleEntry> parse_srt_file(const std::filesystem::path& path) {
    SRTParser parser;
    return parser.parse(path);
}

std::string write_srt(const std::vector<SubtitleEntry>& entries) {
    return SRTWriter::to_string(entries);
}

} // namespace ffsubsync
