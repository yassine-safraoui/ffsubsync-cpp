#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ffsubsync/aligner.h"
#include "ffsubsync/ffmpeg_audio_decoder.h"
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/subtitle_speech.h"
#include "ffsubsync/vad_processor.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    cxxopts::Options options("ffsubsync",
                             "ffsubsync C++ - Subtitle synchronisation tool");

    options.add_options()
        ("reference", "Reference video/audio file",
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

        if (!result.count("reference")) {
            std::cerr << "Error: --reference is required." << std::endl;
            return 1;
        }

        if (!result.count("subs-dir")) {
            std::cerr << "Error: --subs-dir is required." << std::endl;
            return 1;
        }

        std::string reference_path = result["reference"].as<std::string>();
        std::string subs_dir = result["subs-dir"].as<std::string>();
        std::string model_path = result["model"].as<std::string>();

        // Open reference media with FFmpeg.
        ffsubsync::FFmpegAudioDecoder decoder;
        ffsubsync::FFmpegAudioDecoder::Config decoder_config;
        decoder_config.target_sample_rate = 16000;
        decoder_config.target_channels = 1;
        decoder_config.start_seconds = 0.0;

        if (!decoder.open(reference_path, decoder_config)) {
            std::cerr << "Error: failed to open reference file: " << reference_path
                      << " (" << decoder.error_message() << ")" << std::endl;
            return 1;
        }

        // Extract audio speech vector at 100 Hz using streaming VAD.
        ffsubsync::VADProcessor vad(model_path, 16000);
        ffsubsync::FFmpegAudioDecoder::AudioChunk chunk{};

        while (decoder.next(chunk)) {
            vad.feed(chunk.data, static_cast<size_t>(chunk.num_samples));
        }

        vad.flush();
        auto segments = vad.drain_segments();

        auto audio_speech = ffsubsync::VADProcessor::to_binary_vector(
            segments, 16000, 100.0f, decoder.info().duration_seconds);

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
