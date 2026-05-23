#pragma once

#include <spdlog/spdlog.h>

#if defined(__ANDROID__)
#include <spdlog/sinks/android_sink.h>
#endif

#include <mutex>

namespace ffsubsync {

inline void setup_logging() {
    static std::once_flag flag;
    std::call_once(flag, []() {
#if defined(__ANDROID__)
        auto logger = spdlog::android_logger_mt("ffsubsync", "SubtitleSync");
#else
        auto logger = spdlog::stdout_logger_mt("ffsubsync");
#endif
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::warn);
    });
}

}