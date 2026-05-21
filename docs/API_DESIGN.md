# C++ API Design

This document proposes the public C++ API for `libffsubsync`. The design prioritizes:
1. **Simplicity**: Easy to understand and use
2. **Android-friendly**: No exceptions across JNI boundaries, C-compatible interface option
3. **Efficiency**: Move semantics, minimal copies, chunked processing
4. **Testability**: Pure functions where possible, dependency injection

---

## Directory Layout

```
include/ffsubsync/
├── ffsubsync.h          # Main public header (C++ API)
├── ffsubsync_c_api.h    # Optional C API for JNI / other languages
├── constants.h          # Compile-time constants
├── types.h              # Core types (TimePoint, SubtitleEntry, etc.)
├── aligner.h            # FFTAligner, MaxScoreAligner
├── speech.h             # VAD interfaces and implementations
├── subtitles.h          # Subtitle parsers and transformers
├── media.h              # FFmpeg audio extraction
└── pipeline.h           # High-level orchestration
```

---

## Core Types

```cpp
// include/ffsubsync/types.h
#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <filesystem>

namespace ffsubsync {

using TimePoint = std::chrono::milliseconds;

struct SubtitleEntry {
    TimePoint start;
    TimePoint end;
    std::string content;
    
    // Format-specific metadata preserved for round-trip
    struct SRTData { int index; };
    struct ASSData { std::string style; std::string name; int layer; };
    struct VTTData { std::string cue_settings; };
    
    std::variant<std::monostate, SRTData, ASSData, VTTData> format_data;
};

struct AlignmentResult {
    double score = 0.0;           // Cross-correlation score
    double offset_seconds = 0.0;  // Best offset in seconds
    double framerate_ratio = 1.0; // Detected or tried framerate ratio
    bool success = false;
    std::optional<std::string> error_message;
};

struct SyncOptions {
    // Reference
    std::filesystem::path reference_path;
    std::optional<std::string> reference_stream;  // e.g. "0:a:0" for first audio track
    
    // Input/Output
    std::vector<std::filesystem::path> input_subtitles;
    std::optional<std::filesystem::path> output_path;
    bool overwrite_input = false;
    
    // Processing
    int sample_rate = 100;                    // Hz (10ms windows)
    int audio_frame_rate = 48000;             // Hz for audio extraction
    double max_offset_seconds = 60.0;
    double start_seconds = 0.0;
    double apply_offset_seconds = 0.0;
    bool fix_framerate = true;
    bool use_gss = false;                     // Golden-section search
    
    // VAD
    enum class VADType { WebRTC, Auditok, Silero };
    VADType vad = VADType::Silero;            // Default: Sherpa ONNX Silero
    std::string vad_model_path = "models/silero_vad.onnx";
    float non_speech_label = 0.0f;
    
    // Subtitle handling
    std::string input_encoding = "infer";     // "infer" or explicit encoding
    std::string output_encoding = "utf-8";
    double max_subtitle_seconds = 10.0;
    bool strict_parsing = false;
    
    // Output options
    bool merge_with_reference = false;
    bool serialize_speech = false;
    std::optional<double> suppress_output_threshold;
    
    // Callbacks
    using ProgressCallback = std::function<void(const std::string& stage, double fraction)>;
    using LogCallback = std::function<void(const std::string& message)>;
    
    ProgressCallback on_progress;
    LogCallback on_log;
};

} // namespace ffsubsync
```

---

## High-Level API

For most users (including Android via JNI), a single function is sufficient:

```cpp
// include/ffsubsync/ffsubsync.h
#pragma once
#include "ffsubsync/types.h"

namespace ffsubsync {

/**
 * Synchronize subtitles with a video or reference subtitle file.
 * 
 * @param options Configuration options
 * @return Vector of results, one per input subtitle file
 */
std::vector<AlignmentResult> synchronize(const SyncOptions& options);

/**
 * Extract speech activity from a video or audio file.
 * 
 * @param media_path Path to video/audio file (must be 16kHz mono WAV for now)
 * @param options Partial options (only VAD-related fields used)
 * @return Binary speech vector (1.0 = speech, 0.0 = non-speech)
 */
std::vector<float> extract_speech_from_media(
    const std::filesystem::path& media_path,
    const SyncOptions& options);

/**
 * Extract speech activity from subtitle timestamps.
 * 
 * @param subtitles Parsed subtitle entries
 * @param sample_rate Sample rate in Hz (default 100)
 * @return Binary speech vector
 */
std::vector<float> extract_speech_from_subtitles(
    const std::vector<SubtitleEntry>& subtitles,
    int sample_rate = 100);

} // namespace ffsubsync
```

---

## C API (For JNI / FFI)

A C API is essential for Android JNI and language bindings:

```cpp
// include/ffsubsync/ffsubsync_c_api.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
struct FFSyncContext;
struct FFSyncResult;

// Lifecycle
FFSyncContext* ffsubsync_create();
void ffsubsync_destroy(FFSyncContext* ctx);

// Configuration
void ffsubsync_set_reference(FFSyncContext* ctx, const char* path);
void ffsubsync_add_input(FFSyncContext* ctx, const char* path);
void ffsubsync_set_output(FFSyncContext* ctx, const char* path);
void ffsubsync_set_option_int(FFSyncContext* ctx, const char* key, int value);
void ffsubsync_set_option_double(FFSyncContext* ctx, const char* key, double value);
void ffsubsync_set_option_string(FFSyncContext* ctx, const char* key, const char* value);

// Progress callback: void(void* userdata, const char* stage, double fraction)
typedef void (*FFSyncProgressCallback)(void* userdata, const char* stage, double fraction);
void ffsubsync_set_progress_callback(FFSyncContext* ctx, FFSyncProgressCallback cb, void* userdata);

// Execution
int ffsubsync_run(FFSyncContext* ctx);

// Results
size_t ffsubsync_get_result_count(FFSyncContext* ctx);
const FFSyncResult* ffsubsync_get_result(FFSyncContext* ctx, size_t index);

// Result accessors
int ffsubsync_result_get_success(const FFSyncResult* result);
double ffsubsync_result_get_offset_seconds(const FFSyncResult* result);
double ffsubsync_result_get_framerate_ratio(const FFSyncResult* result);
double ffsubsync_result_get_score(const FFSyncResult* result);
const char* ffsubsync_result_get_error(const FFSyncResult* result);

#ifdef __cplusplus
}
#endif
```

---

## Detailed Component APIs

### Subtitle Parser

```cpp
// include/ffsubsync/subtitles.h
namespace ffsubsync {

enum class SubtitleFormat { SRT, ASS, SSA, VTT, Unknown };

class SubtitleParser {
public:
    virtual ~SubtitleParser() = default;
    virtual std::vector<SubtitleEntry> parse(const std::filesystem::path& path) = 0;
    virtual std::vector<SubtitleEntry> parse_string(const std::string& content) = 0;
    virtual SubtitleFormat format() const = 0;
};

// Factory
std::unique_ptr<SubtitleParser> make_parser(SubtitleFormat fmt);
std::unique_ptr<SubtitleParser> make_parser_for_file(const std::filesystem::path& path);

// Concrete implementations (can be used directly)
class SRTParser : public SubtitleParser {
public:
    std::vector<SubtitleEntry> parse(const std::filesystem::path& path) override;
    std::vector<SubtitleEntry> parse_string(const std::string& content) override;
    SubtitleFormat format() const override { return SubtitleFormat::SRT; }
};

// Writer
class SubtitleWriter {
public:
    virtual ~SubtitleWriter() = default;
    virtual void write(const std::vector<SubtitleEntry>& entries,
                       const std::filesystem::path& path,
                       const std::string& encoding = "utf-8") = 0;
    virtual std::string to_string(const std::vector<SubtitleEntry>& entries) = 0;
};

class SRTWriter : public SubtitleWriter {
public:
    void write(const std::vector<SubtitleEntry>& entries,
               const std::filesystem::path& path,
               const std::string& encoding = "utf-8") override;
    std::string to_string(const std::vector<SubtitleEntry>& entries) override;
};

// Transformers
std::vector<SubtitleEntry> shift_subtitles(const std::vector<SubtitleEntry>& subs,
                                            std::chrono::milliseconds offset);

std::vector<SubtitleEntry> scale_subtitles(const std::vector<SubtitleEntry>& subs,
                                            double factor);

std::vector<SubtitleEntry> merge_subtitles(const std::vector<SubtitleEntry>& a,
                                            const std::vector<SubtitleEntry>& b,
                                            bool reference_first = true);

} // namespace ffsubsync
```

### VAD Interface

```cpp
// include/ffsubsync/vad_processor.h
namespace ffsubsync {

/**
 * @brief Wraps Sherpa ONNX Silero VAD to convert raw PCM audio into a binary speech array.
 *
 * Processes audio using the Silero VAD model via Sherpa ONNX.
 * Input must be mono float PCM at 16000 Hz (or 8000 Hz).
 * Outputs a binary speech vector at a configurable sample rate:
 *   1.0f = speech detected
 *   0.0f = no speech
 *
 * Supports both batch (process) and streaming (feed/flush/drain_segments) APIs.
 */
class VADProcessor {
public:
    explicit VADProcessor(const std::string& model_path, int sample_rate = 16000);
    ~VADProcessor();

    // Disable copy
    VADProcessor(const VADProcessor&) = delete;
    VADProcessor& operator=(const VADProcessor&) = delete;

    VADProcessor(VADProcessor&&) noexcept;
    VADProcessor& operator=(VADProcessor&&) noexcept;

    /**
     * @brief Process a contiguous block of float PCM mono samples.
     *
     * Convenience wrapper around feed() + flush() + drain_segments() +
     * to_binary_vector().
     */
    std::vector<float> process(const std::vector<float>& samples,
                                float output_sample_rate = 100.0f);

    // Incremental feed. Can be called with any chunk size.
    void feed(const float* samples, size_t count);

    // Signal end of stream. Required to finalize the last segment.
    void flush();

    // Reset all state for a new audio stream.
    void reset();

    // Pop all completed segments from Sherpa's internal queue.
    std::vector<SpeechSegment> drain_segments();

    // Convert segments to binary speech vector.
    static std::vector<float> to_binary_vector(
        const std::vector<SpeechSegment>& segments,
        int audio_sample_rate,
        float output_sample_rate,
        double total_duration_seconds);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace ffsubsync
```

### Audio Extractor

```cpp
// include/ffsubsync/ffmpeg_audio_decoder.h
namespace ffsubsync {

class FFmpegAudioDecoder {
public:
    struct Config {
        int target_sample_rate = 16000;   // VAD requires 16kHz
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
        const float* data = nullptr;      // mono float PCM
        int num_samples = 0;
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
};

} // namespace ffsubsync
```

### Aligner

```cpp
// include/ffsubsync/aligner.h
namespace ffsubsync {

class FFTAligner {
public:
    struct Result {
        int offset_samples = 0;
        double score = 0.0;
    };

    explicit FFTAligner(std::optional<int> max_offset_samples = std::nullopt);
    ~FFTAligner();

    Result align(const std::vector<float>& reference,
                 const std::vector<float>& candidate);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

class MaxScoreAligner {
public:
    struct Result {
        double score = 0.0;
        int offset_samples = 0;
        double framerate_ratio = 1.0;
    };

    MaxScoreAligner(const FFTAligner& base_aligner,
                    int sample_rate,
                    std::optional<double> max_offset_seconds = std::nullopt);

    Result find_best_alignment(
        const std::vector<float>& reference_speech,
        const std::vector<std::pair<std::vector<float>, double>>& candidates);
        // ^ pairs of (speech_frames, framerate_ratio)

private:
    FFTAligner base_aligner_;
    int sample_rate_;
    std::optional<int> max_offset_samples_;
};

} // namespace ffsubsync
```

---

## Usage Examples

### Desktop CLI (C++)
```cpp
#include <ffsubsync/ffsubsync.h>

int main(int argc, char** argv) {
    ffsubsync::SyncOptions opts;
    opts.reference_path = "movie.mp4";
    opts.input_subtitles = {"subs.srt"};
    opts.output_path = "synced.srt";
    opts.vad = ffsubsync::SyncOptions::VADType::Silero;
    opts.vad_model_path = "models/silero_vad.onnx";
    opts.on_progress = [](const std::string& stage, double f) {
        std::cout << stage << ": " << int(f*100) << "%\n";
    };
    
    auto results = ffsubsync::synchronize(opts);
    
    for (const auto& res : results) {
        if (res.success) {
            std::cout << "Offset: " << res.offset_seconds << "s\n";
            std::cout << "Ratio: " << res.framerate_ratio << "\n";
        } else {
            std::cerr << "Failed: " << res.error_message.value_or("unknown") << "\n";
        }
    }
    
    return 0;
}
```

### Android (Java/Kotlin via JNI)
```kotlin
// Kotlin wrapper around C API
class FfsubsyncSyncer {
    private var context: Long = 0
    
    init {
        context = nativeCreate()
    }
    
    fun setReference(path: String) {
        nativeSetReference(context, path)
    }
    
    fun addInput(path: String) {
        nativeAddInput(context, path)
    }
    
    fun setProgressListener(listener: (stage: String, percent: Int) -> Unit) {
        nativeSetProgressCallback(context) { _, stage, fraction ->
            listener(stage, (fraction * 100).toInt())
        }
    }
    
    fun sync(): List<SyncResult> {
        val error = nativeRun(context)
        if (error != 0) throw SyncException("Sync failed with code $error")
        
        val count = nativeGetResultCount(context)
        return (0 until count).map { 
            SyncResult(
                success = nativeResultGetSuccess(context, it),
                offsetSeconds = nativeResultGetOffset(context, it),
                framerateRatio = nativeResultGetRatio(context, it)
            )
        }
    }
    
    protected fun finalize() {
        nativeDestroy(context)
    }
    
    private external fun nativeCreate(): Long
    private external fun nativeSetReference(ctx: Long, path: String)
    // ... other native methods
}
```

---

## Design Decisions

### 1. Pimpl Idiom
Heavy classes (`FFmpegAudioExtractor`, `VADProcessor`) use the Pimpl (Pointer to Implementation) idiom to:
- Hide Sherpa ONNX headers from public API
- Maintain ABI stability
- Reduce compile times

### 2. No Exceptions Across JNI
The C API uses return codes (`int`) rather than exceptions. The C++ API can use exceptions internally but must catch them at the C boundary.

### 3. Callbacks Over Polling
Progress is delivered via callbacks rather than polling state. This maps naturally to Android's UI thread model.

### 4. Optional Chunked Processing
For very long videos, `extract_chunked` allows processing without loading the entire audio into RAM. This is important on memory-constrained Android devices.

### 5. `std::filesystem`
Using C++17 `std::filesystem::path` instead of `std::string` for paths. This handles Unicode correctly on all platforms.

### 6. Encoding Strategy
Input subtitle encoding detection is automatic by default but can be overridden. Output is always UTF-8 unless explicitly specified.

### 7. VAD Model Path
The VAD model file path is passed via `SyncOptions::vad_model_path` rather than hardcoded. This allows:
- Different model versions
- Platform-specific model locations
- User-provided custom models
