# Implementation Roadmap

A phased approach to migrating FFsubsync from Python to C++. Each phase has clear deliverables, estimated effort, and exit criteria.

---

## Phase 0: Foundation (Week 1-2) ✅ COMPLETE

### Goals
- Set up the C++ project structure
- Establish build system and dependency management
- Create CI/CD pipeline
- Write initial tests

### Tasks
1. **Repository Setup**
   - Create `CMakeLists.txt` with C++17 standard
   - Set up directory structure (`src/`, `include/`, `tests/`, `third_party/`)
   - Add `.gitignore`, `README.md`, `LICENSE`

2. **Dependency Integration**
   - Set up `FetchContent` for: Catch2, cxxopts, spdlog
   - Vendor Kiss FFT in `third_party/kiss_fft/`
   - Add Sherpa ONNX prebuilt binaries for macOS

3. **Build Configurations**
   - Desktop: macOS (primary), Linux, Windows (future)
   - Debug/Release configurations

4. **Initial Testing Infrastructure**
   - Integrate Catch2
   - Create `tests/main.cpp` and a dummy passing test
   - Set up test data directory with sample SRT files

### Deliverables
- Empty but buildable project on all target platforms
- CI passing on push/PR

### Exit Criteria
- `cmake -B build && cmake --build build` succeeds on macOS
- `ctest` runs and passes dummy test

**Status**: ✅ Complete

---

## Phase 1: Core Algorithm (Week 3-5) ✅ COMPLETE

### Goals
- Implement FFT-based alignment
- Implement subtitle speech extraction
- Implement Sherpa ONNX VAD integration
- Build macOS CLI tool
- Achieve parity with Python alignment accuracy

### Tasks

#### 1.1 Constants and Types
- Port `constants.py` to `include/ffsubsync/constants.h`
- Define core types (`SubtitleEntry`, `TimePoint`, `AlignmentResult`)

#### 1.2 FFT Aligner
- Implement `FFTAligner` class using Kiss FFT
- Implement bipolar conversion: `2 * x - 1`
- Implement padding to power-of-2
- Implement cross-correlation via FFT multiplication
- Implement argmax with max_offset clipping

#### 1.3 Max Score Aligner
- Implement `MaxScoreAligner` that tries multiple framerate ratios
- Implement score comparison and best-result selection

#### 1.4 Subtitle Speech Transformer
- Implement `SubtitleSpeechTransformer`
- Generate binary speech vector from `SubtitleEntry` timestamps
- Handle `start_seconds` offset
- Implement metadata detection (skip intro/outro text)

#### 1.5 Sherpa ONNX VAD Integration
- Download `silero_vad.onnx` model
- Create `VADProcessor` wrapper around `sherpa_onnx::cxx::VoiceActivityDetector`
- Feed audio in 512-sample chunks
- Convert speech segments to dense binary vector
- Handle 16kHz mono input requirement

#### 1.6 SRT Parser
- Implement `SRTParser`
- Parse standard SRT format with index, timestamps, content
- Handle multi-line subtitles
- Handle malformed input gracefully (with `strict` mode option)

#### 1.7 macOS CLI Tool
- Implement `ffsubsync_cli` with `cxxopts`
- Parse `--wav`, `--subs-dir`, `--model` arguments
- Pipeline: Read WAV -> VAD -> align each SRT -> print offsets
- Validate against real test data (Copenhagen Test S01E03, La Brea S01E01)

### Tests
- FFT aligner unit tests (offset -1, 0, +1, max_offset clipping)
- Subtitle speech extraction tests
- SRT parser round-trip tests
- VAD processor tests (construction, invalid model, unsupported rate, output sizing)

### Deliverables
- `libffsubsync_core` static library
- `ffsubsync_cli` executable that can:
  - Read a 16kHz mono WAV file
  - Extract speech using Sherpa ONNX VAD
  - Parse SRT files from a directory
  - Compute alignment offsets
  - Print results

### Exit Criteria
- FFT aligner tests pass with correct offsets
- VAD tests pass with Sherpa ONNX model
- CLI produces sensible offsets on real test data:
  - Copenhagen Test: offsets ~0.4s (near-perfect sync)
  - La Brea: two clear clusters at ~+3s and ~+7s

**Status**: ✅ Complete

---

## Phase 2: Media Pipeline (Week 6-8)

### Goals
- Integrate FFmpeg C API for audio extraction
- Support arbitrary video/audio input formats
- Support encoding detection
- CLI can sync SRT against video file directly

### Tasks

#### 2.1 FFmpeg Audio Extractor
- Implement `FFmpegAudioExtractor` using libavformat/libavcodec/libswresample
- Implement `probe()` for duration and stream info
- Implement `extract()` returning `std::vector<int16_t>` mono PCM
- Implement `extract_chunked()` for large files
- Handle start time offset (`-ss` equivalent)
- Handle stream selection (`-map` equivalent)

#### 2.2 Video Speech Transformer
- Implement `VideoSpeechTransformer`
- Orchestrate: extract audio -> resample to 16kHz -> VAD -> binary speech vector
- Handle embedded subtitle streams (try first 5 subtitle streams as fallback)
- Support progress callbacks during extraction

#### 2.3 Encoding Detection
- Integrate `uchardet` or minimal UTF-8 detection
- Try detected encoding, fall back to UTF-8

#### 2.4 CLI Tool Enhancement
- Add full argument parsing with `cxxopts`
- Support video reference, subtitle input, subtitle output
- Add logging with `spdlog`
- Add progress bars with `indicators`

#### 2.5 Subtitle Writer
- Implement `SRTWriter`
- Write synchronized subtitles with proper formatting
- Preserve original encoding or use specified output encoding

### Tests
- End-to-end test with sample video and known-offset subtitle
- Test chunked extraction vs full extraction produce identical results
- Test encoding detection on non-UTF-8 SRT files

### Deliverables
- Desktop CLI `ffsubsync` that matches Python CLI for basic usage
- Can perform: `ffsubsync video.mp4 -i subs.srt -o synced.srt`

### Exit Criteria
- CLI produces output identical to Python `ffsubsync` for SRT inputs
- Integration tests pass on sample data

---

## Phase 3: Advanced Features (Week 9-11)

### Goals
- Framerate ratio inference and correction
- Golden-section search
- ASS/SSA/VTT support
- Subtitle merging
- Serialized speech cache

### Tasks

#### 3.1 Framerate Ratio Handling
- Implement `SubtitleScaler`
- Implement ratio inference from duration comparison
- Implement golden-section search (`golden_section_search.cpp`)
- Try common ratios: 24/23.976, 25/23.976, 25/24 and their inverses

#### 3.2 ASS/SSA/VTT Parser
- Implement lightweight ASS parser (events, styles, timestamps)
- Implement VTT parser
- Generic format auto-detection from file extension

#### 3.3 Subtitle Merger
- Implement `merge_subtitles()` function
- Interleave reference and output subtitles
- Handle overlapping time ranges

#### 3.4 Speech Serialization
- Implement `.npz` equivalent (custom binary format or flatbuffers)
- Save extracted speech for fast re-sync
- Load serialized speech as reference

#### 3.5 Additional VAD Options (Optional)
- Allow tuning VAD threshold, min_speech_duration, etc.
- Explore streaming VAD for very long files

### Tests
- Port `test_integration.py` scenarios
- Test framerate correction on known mismatched files
- Test merge functionality
- Round-trip tests for ASS/VTT

### Deliverables
- Feature parity with Python `ffsubsync` v0.4.x for all common use cases

### Exit Criteria
- All major Python test scenarios have C++ equivalents passing
- CLI supports all major flags from Python version

---

## Phase 4: Android Integration (Week 12-15)

### Goals
- Build library for Android
- Create JNI bindings
- Package as AAR
- Create sample Android app

### Tasks

#### 4.1 Android Build
- Configure CMake for Android NDK (ARM64, x86_64)
- Integrate prebuilt FFmpeg for Android (minimal build)
- Integrate Sherpa ONNX Android binaries (AAR or .so)
- Strip desktop-only features for Android (CLI, progress bars)
- Optimize memory usage for mobile (chunked processing by default)

#### 4.2 C API Layer
- Implement `ffsubsync_c_api.h` functions
- Ensure no exceptions cross the C boundary
- Handle UTF-8 string conversion for JNI

#### 4.3 JNI Bindings
- Create `android/jni/ffsubsync_jni.cpp`
- Map Java `FfsubsyncSyncer` class to C API
- Handle progress callbacks via JNI AttachCurrentThread
- Manage native object lifecycle (finalize/close)

#### 4.4 Java/Kotlin API
- Create `Ffsubsync` Java class wrapping native operations
- Design async API using `AsyncTask` or Kotlin coroutines
- Provide `SyncListener` interface for progress updates

```kotlin
interface SyncListener {
    fun onProgress(stage: String, percent: Int)
    fun onComplete(result: SyncResult)
    fun onError(error: String)
}

class Ffsubsync {
    fun sync(reference: File, subtitle: File, listener: SyncListener? = null): SyncResult
    fun syncAsync(reference: File, subtitle: File, listener: SyncListener)
}
```

#### 4.5 AAR Packaging
- Create Android library module with `CMakeLists.txt`
- Package native libs for all ABIs
- Create Maven/Gradle publication config

#### 4.6 Sample Android App
- Simple UI: pick video, pick subtitle, sync
- Display progress bar during sync
- Show result (offset, ratio)
- Open synced subtitle with external app

### Tests
- Unit tests on Android emulator
- Instrumentation tests with sample media
- Memory leak detection with LeakCanary

### Deliverables
- `ffsubsync-android.aar` library
- Sample app APK
- Integration guide for Android developers

### Exit Criteria
- Sample app can sync a subtitle on a physical Android device
- No crashes, reasonable performance (< 30s for 1-hour video)
- Memory usage stays under 200MB

---

## Phase 5: Polish & Optimization (Week 16+)

### Goals
- Performance optimization
- Additional subtitle formats
- iOS compatibility (stretch goal)
- Documentation

### Tasks

#### 5.1 Performance
- Profile with perf/Android Profiler
- Optimize FFT (consider KFR as upgrade from Kiss FFT)
- Parallelize VAD processing with `std::thread` or `std::async`
- Reduce memory allocations in hot paths

#### 5.2 Additional Formats
- SUB (MicroDVD) support
- SBV (YouTube) support
- TTML support

#### 5.3 iOS (Stretch Goal)
- Configure build for iOS/arm64
- Use Apple's Accelerate framework for FFT (vDSP)
- Package as XCFramework

#### 5.4 Documentation
- API reference (Doxygen)
- Android integration guide
- Migration guide from Python version
- Performance tuning guide

#### 5.5 Release
- Version 1.0.0
- GitHub releases with binaries
- Package managers: vcpkg, Conan (optional)

---

## Effort Summary

| Phase | Duration | Primary Complexity | Status |
|-------|----------|-------------------|--------|
| 0: Foundation | 1-2 weeks | Build system setup | ✅ Complete |
| 1: Core Algorithm | 2-3 weeks | FFT alignment + VAD integration | ✅ Complete |
| 2: Media Pipeline | 2-3 weeks | FFmpeg C API complexity | 🔄 In Progress |
| 3: Advanced Features | 2-3 weeks | ASS parsing, framerate search | ⏳ Pending |
| 4: Android | 3-4 weeks | JNI, NDK, mobile constraints | ⏳ Pending |
| 5: Polish | Ongoing | Performance, documentation | ⏳ Pending |

**Total estimated time to Android MVP: 10-15 weeks** (single developer, full-time)

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| FFmpeg C API is complex | Start with ffmpeg CLI approach (already working), switch to libav in Phase 2 |
| FFT results differ from numpy | Validate with bit-exact tests; small differences are expected due to floating point |
| Sherpa ONNX binary size | ONNX Runtime is ~15MB; for Android, use split APK or dynamic delivery |
| Android memory limits | Use chunked processing; test on low-end devices |
| ASS format complexity | Defer to Phase 3; start with SRT only |
| License compliance (FFmpeg LGPL) | Dynamic linking on Android; provide source offer |
| Model file distribution | Bundle `silero_vad.onnx` (~600KB) in app assets |
