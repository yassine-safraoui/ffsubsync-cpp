# Architecture Mapping: Python -> C++

This document provides a module-by-module breakdown of how each Python component in FFsubsync maps to a C++ equivalent. It includes data structures, algorithmic details, and design rationale.

---

## 1. Core Pipeline (`sklearn_shim.py`)

### Python Design
FFsubsync uses a scikit-learn-inspired `Pipeline` and `TransformerMixin` pattern where each processing step implements `fit()` and `transform()`. The pipeline chains these steps.

### C++ Design
We will **not** replicate the full scikit-learn pipeline pattern. Instead, we use a simplified, explicit composition pattern:

```cpp
namespace ffsubsync {

class Transformer {
public:
    virtual ~Transformer() = default;
    // fit() returns self for chaining; in C++ we don't need this
    virtual void fit(/* input */) = 0;
    virtual auto transform(/* input */) -> /* output */ = 0;
};

// For the pipeline, we use explicit composition in the orchestration layer
// rather than a generic pipeline class. This is more idiomatic in C++ and
// avoids virtual dispatch overhead for what is essentially a fixed 3-step process.

} // namespace ffsubsync
```

**Rationale**: The Python pipeline is dynamic and introspectable. In C++, the processing graph is fixed and known at compile time. Explicit composition with `std::unique_ptr` or value semantics is clearer and faster.

---

## 2. Aligners (`aligners.py`)

### FFTAligner

**Python Algorithm**:
```python
def fit(self, refstring, substring):
    # Convert binary strings to -1/+1 float arrays
    refstring, substring = 2 * np.array(s).astype(float) - 1
    # Pad to power of 2 for FFT
    total_bits = math.log(len(substring) + len(refstring), 2)
    total_length = int(2 ** math.ceil(total_bits))
    extra_zeros = total_length - len(substring) - len(refstring)
    # FFT of padded arrays
    subft = np.fft.fft(np.append(np.zeros(extra_zeros + len(refstring)), substring))
    refft = np.fft.fft(np.flip(np.append(refstring, np.zeros(len(substring) + extra_zeros)), 0))
    # Convolution via multiplication in frequency domain
    convolve = np.real(np.fft.ifft(subft * refft))
    # Find argmax
    best_idx = int(np.argmax(convolve))
    self.best_offset_ = len(convolve) - 1 - best_idx - len(substring)
    self.best_score_ = convolve[best_idx]
```

**C++ Equivalent**:

```cpp
namespace ffsubsync {

class FFTAligner {
public:
    struct Result {
        int offset_samples;
        double score;
    };

    explicit FFTAligner(std::optional<int> max_offset_samples = std::nullopt);
    
    // refstring and substring are binary speech arrays (0.0 or 1.0)
    Result align(const std::vector<float>& refstring, 
                 const std::vector<float>& substring);

private:
    std::optional<int> max_offset_samples_;
};

} // namespace ffsubsync
```

**Implementation Notes**:
- Uses `std::vector<std::complex<float>>` with Kiss FFT
- The padding to power-of-2 is handled manually
- The scoring formula: `score = (# matched 1s) - (# ref 1s matched with sub 0s)` is mathematically equivalent to the cross-correlation of bipolar (-1, +1) sequences
- Eliminate extreme offsets by setting those regions to `-inf` before argmax

### MaxScoreAligner

**Python Design**:
Tries multiple framerate ratios, scores each with `FFTAligner`, and picks the best score.

**C++ Equivalent**:

```cpp
namespace ffsubsync {

class MaxScoreAligner {
public:
    struct ScoredAlignment {
        double score;
        int offset_samples;
        double framerate_ratio;
    };

    MaxScoreAligner(const FFTAligner& base_aligner,
                    int sample_rate,
                    std::optional<double> max_offset_seconds = std::nullopt);

    ScoredAlignment find_best_alignment(
        const std::vector<float>& reference_speech,
        const std::vector<std::vector<float>>& candidate_subtitle_speeches,
        const std::vector<double>& framerate_ratios);

private:
    FFTAligner base_aligner_;
    int sample_rate_;
    std::optional<int> max_offset_samples_;
};

} // namespace ffsubsync
```

---

## 3. Speech Transformers (`speech_transformers.py`)

### VideoSpeechTransformer

**Python Design**:
- Spawns `ffmpeg` subprocess to extract raw PCM audio (`s16le`, mono, at specified frame rate)
- Reads stdout in chunks
- Applies VAD (webrtc/auditok/silero) to each chunk
- Returns binary speech array

**C++ Design**:

Current desktop MVP uses ffmpeg CLI + Sherpa ONNX `ReadWave()`:

```cpp
namespace ffsubsync {

// Step 1: Convert to 16kHz mono WAV with ffmpeg CLI
// ffmpeg -i input.mp4 -ar 16000 -ac 1 -c:a pcm_s16le output.wav

// Step 2: Read WAV with Sherpa ONNX helper
auto wave = sherpa_onnx::cxx::ReadWave("output.wav");

// Step 3: VAD -> binary speech vector
VADProcessor vad("models/silero_vad.onnx", 16000);
std::vector<float> speech = vad.process(wave.samples, 100.0f);

} // namespace ffsubsync
```

For Android, we will switch to FFmpeg C libraries (libav*) for in-process audio extraction.

### VAD Interface

```cpp
namespace ffsubsync {

class VADProcessor {
public:
    explicit VADProcessor(const std::string& model_path, int sample_rate = 16000);
    ~VADProcessor();

    VADProcessor(const VADProcessor&) = delete;
    VADProcessor& operator=(const VADProcessor&) = delete;
    VADProcessor(VADProcessor&&) noexcept;
    VADProcessor& operator=(VADProcessor&&) noexcept;

    // Process float PCM samples. Output at configurable sample rate (default 100 Hz).
    std::vector<float> process(const std::vector<float>& samples, 
                                float output_sample_rate = 100.0f);
    void reset();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace ffsubsync
```

**Sherpa ONNX VAD Integration**:
- Silero VAD operates on 16kHz mono float PCM (also supports 8kHz)
- Window size: 512 samples (configurable, must match `VadModelConfig`)
- Internal buffer size: set to 1 hour (3600s) to process entire files in one shot
- Feeds audio in 512-sample chunks, calls `Flush()` at EOF
- Converts `SpeechSegment` results into dense binary vector at target sample rate

### SubtitleSpeechTransformer

**Python Algorithm**:
```python
# For each subtitle, mark the corresponding time windows as speech (1.0)
# Everything else is 0.0
samples = np.zeros(int(max_time * sample_rate) + 2, dtype=float)
for sub in subs:
    if is_metadata(sub.content): continue
    start = int(round((sub.start - start_seconds) * sample_rate))
    end = start + int(round(duration * sample_rate))
    samples[start:end] = min(1.0 / framerate_ratio, 1.0)
```

**C++ Equivalent**:

```cpp
namespace ffsubsync {

class SubtitleSpeechTransformer {
public:
    struct Result {
        std::vector<float> speech_frames;
        int start_frame;
        int end_frame;
        int num_frames;
    };

    SubtitleSpeechTransformer(int sample_rate = 100, 
                              double start_seconds = 0.0,
                              double framerate_ratio = 1.0);

    Result extract_speech(const std::vector<GenericSubtitle>& subtitles);

private:
    int sample_rate_;
    double start_seconds_;
    double framerate_ratio_;
};

} // namespace ffsubsync
```

---

## 4. Subtitle Parsers (`subtitle_parser.py`, `generic_subtitles.py`)

### GenericSubtitle

**Python**:
```python
class GenericSubtitle:
    def __init__(self, start, end, inner):
        self.start = start  # timedelta
        self.end = end      # timedelta
        self.inner = inner  # srt.Subtitle or pysubs2.SSAEvent
```

**C++**:

```cpp
namespace ffsubsync {

struct SubtitleEntry {
    std::chrono::milliseconds start;
    std::chrono::milliseconds end;
    std::string content;
    
    // Optional: preserve original format-specific data
    std::variant<SRTEntry, ASSEntry, VTTEntry> inner;
};

class SubtitleFile {
public:
    using Iterator = std::vector<SubtitleEntry>::iterator;
    using ConstIterator = std::vector<SubtitleEntry>::const_iterator;

    void add_entry(SubtitleEntry entry);
    SubtitleFile offset(std::chrono::milliseconds delta) const;
    SubtitleFile scale_time(double factor) const;
    
    Iterator begin();
    Iterator end();
    ConstIterator begin() const;
    ConstIterator end() const;
    
    void write_srt(const std::filesystem::path& path, const std::string& encoding = "utf-8") const;
    void write_ass(const std::filesystem::path& path, const std::string& encoding = "utf-8") const;

private:
    std::vector<SubtitleEntry> entries_;
    std::string format_;
    std::string encoding_;
};

} // namespace ffsubsync
```

### SRT Parser

**Python** uses the `srt` library. The format is straightforward:
```
1
00:00:00,178 --> 00:00:01,416
Hello world

2
00:00:02,000 --> 00:00:04,500
Second line
```

**C++ Implementation**: Custom parser ~150-200 lines using `std::regex` or manual parsing.

### ASS/SSA Parser

**Python** uses `pysubs2` which handles the complex ASS format.

**C++ Options**:
1. **libass**: Full-featured C ASS renderer/parser (but it's a renderer, not just a parser)
2. **Custom lightweight parser**: Parse only what we need (events, timestamps, styles)
3. **Defer to Phase 2**: Start with SRT only

**Recommendation**: Start with SRT in Phase 1. For Phase 2, either write a lightweight ASS parser or investigate if `libass` can be used without its renderer.

### Encoding Detection

**Python** tries `cchardet`, then `charset_normalizer`, then `chardet`.

**C++ Options**:
1. **uchardet** (Mozilla): C++ library, fast, permissive license
2. **ICU**: Heavy but comprehensive
3. **Minimal**: Try UTF-8 first, then fall back to system locale

**Recommendation**: Use `uchardet` or minimal UTF-8 detection. Most modern subtitles are UTF-8 anyway.

---

## 5. Subtitle Transformers (`subtitle_transformers.py`)

### SubtitleShifter

**Python**:
```python
class SubtitleShifter:
    def fit(self, subs):
        self.subs_ = subs.offset(self.td_seconds)
```

**C++**:
```cpp
SubtitleFile SubtitleShifter::shift(const SubtitleFile& subs, 
                                     std::chrono::seconds offset) {
    return subs.offset(std::chrono::milliseconds(offset));
}
```

### SubtitleScaler

**Python**:
```python
class SubtitleScaler:
    def fit(self, subs):
        for sub in subs:
            scaled_subs.append(GenericSubtitle(
                sub.start * scale_factor,
                sub.end * scale_factor,
                sub.inner
            ))
```

**C++**:
```cpp
SubtitleFile SubtitleScaler::scale(const SubtitleFile& subs, double factor) {
    return subs.scale_time(factor);
}
```

### SubtitleMerger

**Python** has a complex merge algorithm that interleaves two subtitle streams.

**C++**: Implement the same interleaving logic using `std::vector` iterators.

---

## 6. FFmpeg Integration (`ffmpeg_utils.py`, `speech_transformers.py`)

### Python Approach
Spawns `ffmpeg` and `ffprobe` as subprocesses, communicates via stdin/stdout.

### C++ Desktop Approach (FFmpeg C API)

We use `libavformat`, `libavcodec`, `libswresample`, and `libavutil` directly via the FFmpeg C API. FFmpeg is a system dependency on desktop (installed via `brew` or `apt`).

```cpp
namespace ffsubsync {

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
        int num_samples = 0;
    };

    FFmpegAudioDecoder();
    ~FFmpegAudioDecoder();

    bool open(const std::filesystem::path& path, const Config& config);
    bool open(int fd, const Config& config);  // Android JNI
    void close();
    bool next(AudioChunk& chunk);

    const StreamInfo& info() const;
    bool is_open() const;
    std::string error_message() const;
};

} // namespace ffsubsync
```

**Implementation details**:
- Open file with `avformat_open_input`
- Open POSIX fd with custom `AVIOContext` via `avio_alloc_context`
- Find audio stream with `avformat_find_stream_info`
- Decode with `avcodec_send_packet` / `avcodec_receive_frame`
- Resample to 16kHz mono float (`AV_SAMPLE_FMT_FLT`) with `swr_convert`
- Frame-by-frame iteration via `next()` — no large in-memory buffer
- RAII wrappers for all FFmpeg objects

### C++ Android Approach (Phase 4)

Android FFmpeg integration uses prebuilt static libraries per ABI from `ffmpeg-android-maker`.
Libraries are committed to `third_party/ffmpeg/android/{abi}/`.
CMake uses `find_library()` against these prebuilt paths when `FFSUBSYNC_BUILD_ANDROID=ON`.

---

## 7. Golden Section Search (`golden_section_search.py`)

**Python** implements the standard golden-section search to find optimal framerate ratio.

**C++**: Straightforward port using `std::function` for the objective function.

```cpp
namespace ffsubsync {

std::pair<double, double> golden_section_search(
    std::function<double(double, bool)> objective,
    double a, double b, double tol = 1e-4);

} // namespace ffsubsync
```

---

## 8. Main Orchestration (`ffsubsync.py`)

### Python Flow
```
1. Parse CLI arguments
2. Make reference pipe:
   - If reference is subtitle: parse -> extract speech
   - If reference is video: extract audio -> VAD -> speech
3. For each input subtitle:
   a. Make subtitle pipe: parse -> scale -> extract speech
   b. Try multiple framerate ratios
   c. FFT align reference vs subtitle speech
   d. Pick best score
   e. Shift (and optionally merge) subtitle
   f. Write output
```

### C++ Desktop CLI Flow
```cpp
int main(int argc, char** argv) {
    // 1. Parse args with cxxopts
    auto args = parse_args(argc, argv);
    
    // 2. Open reference media with FFmpeg (video or audio)
    FFmpegAudioDecoder decoder;
    FFmpegAudioDecoder::Config decoder_config;
    decoder_config.target_sample_rate = 16000;
    decoder_config.target_channels = 1;
    if (!decoder.open(args.reference_path, decoder_config)) { error; }
    
    // 3. Extract audio speech vector via streaming VAD
    VADProcessor vad(args.model_path, 16000);
    FFmpegAudioDecoder::AudioChunk chunk{};
    while (decoder.next(chunk)) {
        vad.feed(chunk.data, static_cast<size_t>(chunk.num_samples));
    }
    vad.flush();
    auto segments = vad.drain_segments();
    auto audio_speech = VADProcessor::to_binary_vector(
        segments, 16000, 100.0f, decoder.info().duration_seconds);
    
    // 4. For each SRT in subs-dir:
    for (const auto& srt_file : list_srt_files(args.subs_dir)) {
        auto subs = SRTParser().parse(srt_file);
        auto subtitle_speech = SubtitleSpeechTransformer().extract_speech(subs);
        
        // 5. Align
        FFTAligner aligner;
        auto result = aligner.align(audio_speech, subtitle_speech);
        
        // 6. Print results
        double offset_seconds = result.offset_samples / 100.0;
        std::cout << srt_file << " -> offset: " << offset_seconds 
                  << "s, score: " << result.score << "\n";
    }
    
    return 0;
}
```

---

## 9. Constants (`constants.py`)

Python constants become C++ `constexpr` values in a header:

```cpp
// include/ffsubsync/constants.h
#pragma once

namespace ffsubsync {

inline constexpr int SAMPLE_RATE = 100;  // Hz (10ms windows)
inline constexpr int DEFAULT_FRAME_RATE = 48000;  // Hz for audio extraction
inline constexpr float DEFAULT_NON_SPEECH_LABEL = 0.0f;
inline constexpr int DEFAULT_MAX_SUBTITLE_SECONDS = 10;
inline constexpr int DEFAULT_START_SECONDS = 0;
inline constexpr double DEFAULT_SCALE_FACTOR = 1.0;
inline constexpr int DEFAULT_MAX_OFFSET_SECONDS = 60;
inline constexpr int DEFAULT_APPLY_OFFSET_SECONDS = 0;
inline constexpr const char* DEFAULT_VAD = "subs_then_webrtc";

inline constexpr std::array<double, 3> FRAMERATE_RATIOS = {
    24.0 / 23.976,
    25.0 / 23.976,
    25.0 / 24.0
};

inline constexpr std::array<const char*, 4> SUBTITLE_EXTENSIONS = {
    "srt", "ass", "ssa", "sub"
};

} // namespace ffsubsync
```
