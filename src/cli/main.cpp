#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "sherpa-onnx/c-api/cxx-api.h"

#include "ffsubsync/aligner.h"
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/subtitle_speech.h"
#include "ffsubsync/vad_processor.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cxxopts::Options options("ffsubsync",
                             "ffsubsync C++ - Subtitle synchronisation tool");

    options.add_options()
        ("wav", "Reference WAV audio file (16kHz mono)",
         cxxopts::value<std::string>())
        ("subs-dir", "Directory containing SRT subtitle files",
         cxxopts::value<std::string>())
        ("model",
         "Path to silero_vad.onnx model",
         cxxopts::value<std::string>()->default_value("models/silero_vad.onnx"))
        ("h,help", "Print usage");

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (!result.count("wav")) {
            std::cerr << "Error: --wav is required." << std::endl;
            return 1;
        }

        if (!result.count("subs-dir")) {
            std::cerr << "Error: --subs-dir is required." << std::endl;
            return 1;
        }

        std::string wav_path = result["wav"].as<std::string>();
        std::string subs_dir = result["subs-dir"].as<std::string>();
        std::string model_path = result["model"].as<std::string>();

        // Read WAV using Sherpa ONNX helper.
        auto wave = sherpa_onnx::cxx::ReadWave(wav_path);
        if (wave.samples.empty()) {
            std::cerr << "Error: failed to read WAV file: " << wav_path
                      << std::endl;
            return 1;
        }

        if (wave.sample_rate != 16000) {
            std::cerr << "Error: WAV file must be 16kHz mono. Got "
                      << wave.sample_rate << " Hz." << std::endl;
            return 1;
        }

        // Extract audio speech vector at 100 Hz to match subtitle speech.
        ffsubsync::VADProcessor vad(model_path, wave.sample_rate);
        std::vector<float> audio_speech = vad.process(wave.samples, 100.0f);

        std::cout << "Audio: " << audio_speech.size()
                  << " frames (" << (audio_speech.size() / 100.0)
                  << "s @ 100Hz)" << std::endl;

        // Collect SRT files.
        std::vector<fs::path> srt_files;
        for (const auto& entry : fs::directory_iterator(subs_dir)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".srt") {
                srt_files.push_back(entry.path());
            }
        }

        if (srt_files.empty()) {
            std::cerr << "Error: no .srt files found in " << subs_dir
                      << std::endl;
            return 1;
        }

        ffsubsync::FFTAligner aligner;
        ffsubsync::SRTParser parser;

        for (const auto& srt_path : srt_files) {
            auto subtitles = parser.parse(srt_path);
            auto subtitle_speech =
                ffsubsync::extract_speech(subtitles, 100);

            auto alignment = aligner.align(audio_speech, subtitle_speech);
            double offset_seconds = alignment.offset_samples / 100.0;

            std::cout << srt_path.filename().string()
                      << " -> offset: " << offset_seconds << "s"
                      << ", score: " << alignment.score << std::endl;
        }

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        std::cerr << options.help() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
