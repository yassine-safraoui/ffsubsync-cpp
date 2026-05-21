# Code Map: Python to C++

A detailed line-by-line mapping of the Python source code to proposed C++ implementations.

---

## `ffsubsync/constants.py` -> `include/ffsubsync/constants.h`

| Python Line | C++ Equivalent |
|-------------|---------------|
| `SAMPLE_RATE: int = 100` | `inline constexpr int SAMPLE_RATE = 100;` |
| `FRAMERATE_RATIOS: List[float] = [...]` | `inline constexpr std::array<double, 3> FRAMERATE_RATIOS = {...};` |
| `DEFAULT_FRAME_RATE: int = 48000` | `inline constexpr int DEFAULT_FRAME_RATE = 48000;` |
| `DEFAULT_VAD: str = "subs_then_webrtc"` | `inline constexpr const char* DEFAULT_VAD = "subs_then_webrtc";` |
| `SUBTITLE_EXTENSIONS: Tuple[str, ...] = (...)` | `inline constexpr std::array<const char*, 4> SUBTITLE_EXTENSIONS = {...};` |

---

## `ffsubsync/aligners.py`

### `FFTAligner.__init__` (lines 24-29)
```cpp
class FFTAligner {
public:
    explicit FFTAligner(std::optional<int> max_offset_samples = std::nullopt);
private:
    std::optional<int> max_offset_samples_;
    int best_offset_ = 0;
    double best_score_ = 0.0;
};
```

### `FFTAligner._eliminate_extreme_offsets` (lines 31-43)
```cpp
std::vector<std::complex<float>> eliminate_extreme_offsets(
    std::vector<std::complex<float>> convolve,
    size_t substring_len) {
    if (!max_offset_samples_) return convolve;
    
    auto offset_to_index = [&](int offset) {
        return static_cast<int>(convolve.size()) - 1 + offset - static_cast<int>(substring_len);
    };
    
    int min_idx = offset_to_index(-max_offset_samples_.value());
    int max_idx = offset_to_index(max_offset_samples_.value());
    
    for (int i = 0; i <= min_idx && i < static_cast<int>(convolve.size()); ++i) {
        convolve[i] = -std::numeric_limits<float>::infinity();
    }
    for (int i = max_idx; i < static_cast<int>(convolve.size()); ++i) {
        convolve[i] = -std::numeric_limits<float>::infinity();
    }
    return convolve;
}
```

### `FFTAligner._compute_argmax` (lines 45-48)
```cpp
void compute_argmax(const std::vector<std::complex<float>>& convolve, size_t substring_len) {
    auto it = std::max_element(convolve.begin(), convolve.end(),
        [](auto a, auto b) { return a.real() < b.real(); });
    int best_idx = static_cast<int>(std::distance(convolve.begin(), it));
    best_offset_ = static_cast<int>(convolve.size()) - 1 - best_idx - static_cast<int>(substring_len);
    best_score_ = it->real();
}
```

### `FFTAligner.fit` (lines 50-71) — Core Algorithm
```cpp
Result align(const std::vector<float>& refstring, const std::vector<float>& substring) {
    // Convert to bipolar: 2*x - 1
    std::vector<float> ref_bipolar(refstring.size());
    std::vector<float> sub_bipolar(substring.size());
    std::transform(refstring.begin(), refstring.end(), ref_bipolar.begin(),
                   [](float x) { return 2.0f * x - 1.0f; });
    std::transform(substring.begin(), substring.end(), sub_bipolar.begin(),
                   [](float x) { return 2.0f * x - 1.0f; });
    
    // Pad to power of 2
    size_t total_bits = static_cast<size_t>(std::ceil(std::log2(sub_bipolar.size() + ref_bipolar.size())));
    size_t total_length = 1ULL << total_bits;
    size_t extra_zeros = total_length - sub_bipolar.size() - ref_bipolar.size();
    
    // Prepare arrays for FFT (Kiss FFT)
    std::vector<kiss_fft_cpx> sub_fft(total_length);
    std::vector<kiss_fft_cpx> ref_fft(total_length);
    
    // sub: [zeros(extra_zeros + ref_len), substring]
    size_t sub_start = extra_zeros + ref_bipolar.size();
    for (size_t i = 0; i < sub_bipolar.size(); ++i) {
        sub_fft[sub_start + i].r = sub_bipolar[i];
        sub_fft[sub_start + i].i = 0.0f;
    }
    
    // ref: flip([refstring, zeros(sub_len + extra_zeros)])
    for (size_t i = 0; i < ref_bipolar.size(); ++i) {
        ref_fft[i].r = ref_bipolar[i];
        ref_fft[i].i = 0.0f;
    }
    std::vector<kiss_fft_cpx> ref_flipped(total_length);
    for (size_t i = 0; i < total_length; ++i) {
        ref_flipped[i] = ref_fft[total_length - 1 - i];
    }
    
    // Execute FFT
    kiss_fft_cfg fwd_cfg = kiss_fft_alloc(static_cast<int>(total_length), 0, nullptr, nullptr);
    kiss_fft_cfg inv_cfg = kiss_fft_alloc(static_cast<int>(total_length), 1, nullptr, nullptr);
    
    std::vector<kiss_fft_cpx> sub_out(total_length);
    std::vector<kiss_fft_cpx> ref_out(total_length);
    kiss_fft(fwd_cfg, sub_fft.data(), sub_out.data());
    kiss_fft(fwd_cfg, ref_flipped.data(), ref_out.data());
    
    // Multiply in frequency domain
    for (size_t i = 0; i < total_length; ++i) {
        float a = sub_out[i].r, b = sub_out[i].i;
        float c = ref_out[i].r, d = ref_out[i].i;
        sub_out[i].r = a * c - b * d;
        sub_out[i].i = a * d + b * c;
    }
    
    // Inverse FFT
    std::vector<kiss_fft_cpx> convolve(total_length);
    kiss_fft(inv_cfg, sub_out.data(), convolve.data());
    
    float norm = 1.0f / static_cast<float>(total_length);
    std::vector<std::complex<float>> convolve_cpp(total_length);
    for (size_t i = 0; i < total_length; ++i) {
        convolve_cpp[i] = std::complex<float>(convolve[i].r * norm, convolve[i].i * norm);
    }
    
    kiss_fft_free(fwd_cfg);
    kiss_fft_free(inv_cfg);
    
    // Find best alignment
    auto clipped = eliminate_extreme_offsets(convolve_cpp, substring.size());
    compute_argmax(clipped, substring.size());
    
    return {best_offset_, best_score_};
}
```

### `MaxScoreAligner.fit` (lines 122-143)
```cpp
Result find_best_alignment(
    const std::vector<float>& reference,
    const std::vector<std::pair<std::vector<float>, double>>& candidates) {
    
    std::vector<std::tuple<double, int, double>> scores;  // score, offset, ratio
    
    for (const auto& [speech, ratio] : candidates) {
        auto [score, offset] = base_aligner_.align(reference, speech);
        scores.emplace_back(score, offset, ratio);
    }
    
    // Filter by max_offset if specified
    if (max_offset_samples_) {
        auto it = std::remove_if(scores.begin(), scores.end(),
            [&](const auto& s) { return std::abs(std::get<1>(s)) > max_offset_samples_.value(); });
        scores.erase(it, scores.end());
    }
    
    if (scores.empty()) {
        throw FailedToFindAlignmentException("No valid alignment found");
    }
    
    // Find max score
    auto best = *std::max_element(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    
    return {std::get<0>(best), std::get<1>(best), std::get<2>(best)};
}
```

### `MaxScoreAligner.fit_gss` (lines 102-120)
```cpp
void fit_gss(const std::vector<float>& reference,
             std::function<std::vector<float>(double)> pipe_maker) {
    auto objective = [&](double ratio, bool is_last) -> double {
        auto speech = pipe_maker(ratio);
        auto [score, offset] = base_aligner_.align(reference, speech);
        if (is_last) {
            gss_scores_.emplace_back(score, offset, ratio);
        }
        return -score;  // minimize negative score
    };
    
    auto [a, b] = golden_section_search(objective, MIN_FRAMERATE_RATIO, MAX_FRAMERATE_RATIO);
}
```

---

## `ffsubsync/speech_transformers.py`

### `make_subtitle_speech_pipeline` (lines 34-76)
```cpp
std::vector<SubtitleEntry> parse_subtitle(const std::filesystem::path& path) {
    auto parser = ffsubsync::make_parser_for_file(path);
    return parser->parse(path);
}

std::vector<float> make_subtitle_speech_pipeline(
    const std::vector<SubtitleEntry>& subs,
    int sample_rate = 100,
    double start_seconds = 0.0,
    double scale_factor = 1.0) {
    
    auto scaled = (scale_factor != 1.0) 
        ? ffsubsync::scale_subtitles(subs, scale_factor) 
        : subs;
    
    ffsubsync::SubtitleSpeechTransformer transformer(sample_rate, start_seconds, scale_factor);
    return transformer.extract_speech(scaled);
}
```

### Sherpa ONNX VAD Integration (replaces `_make_webrtcvad_detector`)

```cpp
// include/ffsubsync/vad_processor.h
class VADProcessor {
public:
    explicit VADProcessor(const std::string& model_path, int sample_rate = 16000);
    ~VADProcessor();

    VADProcessor(const VADProcessor&) = delete;
    VADProcessor& operator=(const VADProcessor&) = delete;
    VADProcessor(VADProcessor&&) noexcept;
    VADProcessor& operator=(VADProcessor&&) noexcept;

    std::vector<float> process(const std::vector<float>& samples, 
                                float output_sample_rate = 100.0f);
    void reset();

private:
    struct Impl;
    Impl* impl_;
};
```

```cpp
// src/speech/vad_processor.cpp
std::vector<float> VADProcessor::process(const std::vector<float>& samples,
                                          float output_sample_rate) {
    // Reset state for new audio
    impl_->vad->Reset();
    
    // Feed samples in 512-sample chunks
    constexpr int32_t kWindowSize = 512;
    size_t i = 0;
    while (i + kWindowSize <= samples.size()) {
        impl_->vad->AcceptWaveform(samples.data() + i, kWindowSize);
        i += kWindowSize;
    }
    // Tail chunk
    if (i < samples.size()) {
        impl_->vad->AcceptWaveform(samples.data() + i, 
                                    static_cast<int32_t>(samples.size() - i));
    }
    
    impl_->vad->Flush();
    
    // Build dense binary speech vector
    double total_duration = static_cast<double>(samples.size()) / impl_->sample_rate;
    int num_output_samples = static_cast<int>(std::ceil(total_duration * output_sample_rate));
    std::vector<float> speech_vector(num_output_samples, 0.0f);
    
    while (!impl_->vad->IsEmpty()) {
        auto segment = impl_->vad->Front();
        double start_time = static_cast<double>(segment.start) / impl_->sample_rate;
        double end_time = start_time + static_cast<double>(segment.samples.size()) / impl_->sample_rate;
        
        int start_idx = static_cast<int>(std::floor(start_time * output_sample_rate));
        int end_idx = static_cast<int>(std::ceil(end_time * output_sample_rate));
        
        if (start_idx < 0) start_idx = 0;
        if (end_idx > num_output_samples) end_idx = num_output_samples;
        
        for (int idx = start_idx; idx < end_idx; ++idx) {
            speech_vector[idx] = 1.0f;
        }
        
        impl_->vad->Pop();
    }
    
    return speech_vector;
}
```

### `VideoSpeechTransformer.fit` (lines 304-437)
```cpp
std::vector<float> VideoSpeechTransformer::extract_speech(
    const std::filesystem::path& video_path, double start_seconds) {
    
    // Step 1: Convert to 16kHz mono WAV with ffmpeg CLI
    std::string wav_path = video_path.string() + ".16k.wav";
    std::string cmd = "ffmpeg -i \"" + video_path.string() + "\" "
                      "-ar 16000 -ac 1 -c:a pcm_s16le \"" + wav_path + "\" -y";
    std::system(cmd.c_str());
    
    // Step 2: Read WAV with Sherpa ONNX
    auto wave = sherpa_onnx::cxx::ReadWave(wav_path);
    if (wave.samples.empty()) {
        throw std::runtime_error("Failed to read converted WAV");
    }
    
    // Step 3: Run VAD
    VADProcessor vad("models/silero_vad.onnx", 16000);
    return vad.process(wave.samples, 100.0f);
}
```

### `SubtitleSpeechTransformer.fit` (lines 481-504)
```cpp
SubtitleSpeechTransformer::Result SubtitleSpeechTransformer::extract_speech(
    const std::vector<SubtitleEntry>& subtitles) {
    
    // Find max time
    TimePoint max_time{0};
    for (const auto& sub : subtitles) {
        max_time = std::max(max_time, sub.end);
    }
    
    int max_samples = static_cast<int>(max_time.count() * sample_rate_ / 1000) + 2;
    std::vector<float> samples(max_samples, 0.0f);
    
    int start_frame = std::numeric_limits<int>::max();
    int end_frame = 0;
    
    for (size_t i = 0; i < subtitles.size(); ++i) {
        const auto& sub = subtitles[i];
        
        // Skip metadata at beginning/end
        if (is_metadata(sub.content, i == 0 || i + 1 == subtitles.size())) {
            continue;
        }
        
        double start_sec = (sub.start.count() / 1000.0) - start_seconds_;
        double duration = (sub.end.count() - sub.start.count()) / 1000.0;
        
        int start = static_cast<int>(std::round(start_sec * sample_rate_));
        int end = start + static_cast<int>(std::round(duration * sample_rate_));
        
        start_frame = std::min(start_frame, start);
        end_frame = std::max(end_frame, end);
        
        float value = std::min(1.0f / static_cast<float>(framerate_ratio_), 1.0f);
        for (int j = start; j < end && j < max_samples; ++j) {
            samples[j] = value;
        }
    }
    
    return {samples, start_frame, end_frame, end_frame - start_frame};
}
```

---

## `ffsubsync/subtitle_parser.py`

### `GenericSubtitleParser.fit` (lines 90-153)
```cpp
std::vector<SubtitleEntry> GenericSubtitleParser::parse(
    const std::filesystem::path& path) {
    
    // Read file
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    
    // Detect encoding if needed
    std::string encoding = encoding_;
    if (encoding == "infer") {
        encoding = detect_encoding(raw);
    }
    
    // Decode
    std::string content = decode_with_encoding(raw, encoding);
    
    // Parse based on format
    std::vector<SubtitleEntry> entries;
    if (format_ == SubtitleFormat::SRT) {
        entries = parse_srt(content);
    } else if (format_ == SubtitleFormat::ASS || format_ == SubtitleFormat::SSA) {
        entries = parse_ass(content);
    } else {
        throw std::runtime_error("Unsupported format");
    }
    
    // Preprocess: apply start_seconds and max_subtitle_seconds
    return preprocess_subs(entries, max_subtitle_seconds_, start_seconds_);
}
```

---

## `ffsubsync/subtitle_transformers.py`

### `SubtitleShifter.fit` (lines 13-26)
```cpp
std::vector<SubtitleEntry> shift_subtitles(
    const std::vector<SubtitleEntry>& subs,
    std::chrono::milliseconds offset) {
    
    std::vector<SubtitleEntry> result;
    result.reserve(subs.size());
    
    for (const auto& sub : subs) {
        result.push_back({
            sub.start + offset,
            sub.end + offset,
            sub.content,
            sub.format_data
        });
    }
    
    return result;
}
```

### `SubtitleScaler.fit` (lines 29-50)
```cpp
std::vector<SubtitleEntry> scale_subtitles(
    const std::vector<SubtitleEntry>& subs,
    double factor) {
    
    std::vector<SubtitleEntry> result;
    result.reserve(subs.size());
    
    for (const auto& sub : subs) {
        double start_sec = sub.start.count() / 1000.0 * factor;
        double end_sec = sub.end.count() / 1000.0 * factor;
        
        result.push_back({
            std::chrono::milliseconds(static_cast<long long>(start_sec * 1000)),
            std::chrono::milliseconds(static_cast<long long>(end_sec * 1000)),
            sub.content,
            sub.format_data
        });
    }
    
    return result;
}
```

### `SubtitleMerger.fit` (lines 53-122)
```cpp
std::vector<SubtitleEntry> merge_subtitles(
    const std::vector<SubtitleEntry>& reference,
    const std::vector<SubtitleEntry>& output,
    bool reference_first) {
    
    const auto& first = reference_first ? reference : output;
    const auto& second = reference_first ? output : reference;
    
    std::vector<SubtitleEntry> merged;
    
    auto ita = first.begin();
    auto itb = second.begin();
    
    while (ita != first.end() || itb != second.end()) {
        if (ita == first.end()) {
            merged.push_back(*itb++);
        } else if (itb == second.end()) {
            merged.push_back(*ita++);
        } else {
            // Interleave based on start times
            // ... (complex merge logic from Python)
        }
    }
    
    return merged;
}
```

---

## `ffsubsync/ffmpeg_utils.py`

### `ffmpeg_bin_path` (lines 69-87)
```cpp
std::filesystem::path find_ffmpeg_binary(const std::string& name,
                                         bool gui_mode,
                                         std::optional<std::filesystem::path> resources_path) {
#ifdef _WIN32
    std::string bin_name = name + ".exe";
#else
    std::string bin_name = name;
#endif
    
    if (resources_path) {
        if (!std::filesystem::is_directory(*resources_path)) {
            if (name == "ffmpeg") {
                return *resources_path;
            }
            resources_path = resources_path->parent_path();
        }
        return *resources_path / bin_name;
    }
    
    // Check environment variable
    const char* env_path = std::getenv("ffsubsync_resources_xj48gjdkl340");
    if (env_path && strlen(env_path) > 0) {
        return std::filesystem::path(env_path) / "ffmpeg-bin" / bin_name;
    }
    
    // Fall back to PATH
    return bin_name;
}
```

---

## `ffsubsync/ffsubsync.py` (Main Orchestration)

### Desktop CLI (`src/cli/main.cpp`)
```cpp
int main(int argc, char** argv) {
    // Parse args with cxxopts
    cxxopts::Options options("ffsubsync", "Subtitle synchronisation tool");
    options.add_options()
        ("wav", "Reference WAV audio file (16kHz mono)", cxxopts::value<std::string>())
        ("subs-dir", "Directory containing SRT subtitle files", cxxopts::value<std::string>())
        ("model", "Path to silero_vad.onnx model", 
         cxxopts::value<std::string>()->default_value("models/silero_vad.onnx"))
        ("h,help", "Print usage");
    
    auto result = options.parse(argc, argv);
    
    // Read WAV
    auto wave = sherpa_onnx::cxx::ReadWave(result["wav"].as<std::string>());
    if (wave.sample_rate != 16000) { /* error */ }
    
    // Extract audio speech
    ffsubsync::VADProcessor vad(result["model"].as<std::string>(), 16000);
    auto audio_speech = vad.process(wave.samples, 100.0f);
    
    // Process each SRT
    ffsubsync::FFTAligner aligner;
    ffsubsync::SRTParser parser;
    
    for (const auto& srt_path : list_srt_files(result["subs-dir"].as<std::string>())) {
        auto subtitles = parser.parse(srt_path);
        auto subtitle_speech = ffsubsync::extract_speech(subtitles, 100);
        
        auto alignment = aligner.align(audio_speech, subtitle_speech);
        double offset_seconds = alignment.offset_samples / 100.0;
        
        std::cout << srt_path.filename().string()
                  << " -> offset: " << offset_seconds << "s"
                  << ", score: " << alignment.score << std::endl;
    }
    
    return 0;
}
```

---

## `ffsubsync/golden_section_search.py`

### `gss` function (lines 15-74)
```cpp
std::pair<double, double> golden_section_search(
    std::function<double(double, bool)> f,
    double a, double b, double tol = 1e-4) {
    
    static constexpr double invphi = (std::sqrt(5.0) - 1.0) / 2.0;
    static constexpr double invphi2 = (3.0 - std::sqrt(5.0)) / 2.0;
    
    a = std::min(a, b);
    b = std::max(a, b);
    double h = b - a;
    
    if (h <= tol) return {a, b};
    
    int n = static_cast<int>(std::ceil(std::log(tol / h) / std::log(invphi)));
    
    double c = a + invphi2 * h;
    double d = a + invphi * h;
    double yc = f(c, n == 1);
    double yd = f(d, n == 1);
    
    for (int k = 0; k < n - 1; ++k) {
        if (yc < yd) {
            b = d;
            d = c;
            yd = yc;
            h *= invphi;
            c = a + invphi2 * h;
            yc = f(c, k == n - 2);
        } else {
            a = c;
            c = d;
            yc = yd;
            h *= invphi;
            d = a + invphi * h;
            yd = f(d, k == n - 2);
        }
    }
    
    if (yc < yd) return {a, d};
    else return {c, b};
}
```

---

## Key Algorithms Summary

| Algorithm | Python Location | C++ Location | Complexity |
|-----------|----------------|--------------|------------|
| Bipolar conversion | `aligners.py:56` | `fft_aligner.cpp` | O(n) |
| FFT padding | `aligners.py:59-60` | `fft_aligner.cpp` | O(n) |
| FFT cross-correlation | `aligners.py:61-65` | `fft_aligner.cpp` | O(n log n) |
| Argmax with clipping | `aligners.py:41-48` | `fft_aligner.cpp` | O(n) |
| Sherpa ONNX VAD | New | `vad_processor.cpp` | O(n) |
| Subtitle->speech | `speech_transformers.py:486-504` | `subtitle_speech.cpp` | O(n) |
| Golden-section search | `golden_section_search.py:15-74` | `golden_section_search.cpp` | O(log n) |
| SRT parsing | `subtitle_parser.py:115-118` | `srt_parser.cpp` | O(n) |
| Subtitle shifting | `subtitle_transformers.py:21-22` | `subtitle_transformer.cpp` | O(n) |
| Subtitle scaling | `subtitle_transformers.py:36-46` | `subtitle_transformer.cpp` | O(n) |
| FFmpeg audio extraction | `speech_transformers.py:346-429` | `ffmpeg_wrapper.cpp` (future) | O(n) |
