# FFsubsync C++

> A ground-up C++ reimplementation of the [ffsubsync](https://github.com/smacke/ffsubsync) Python project, built to run natively on Android and other platforms without a Python runtime.

## What FFsubsync Does

FFsubsync aligns subtitles to video by:

1. **Extracting speech segments** from the video/audio track using Voice Activity Detection (VAD)
2. **Converting subtitles** into a binary "speech/not-speech" timeline
3. **Aligning** the two binary sequences using FFT-based cross-correlation to find the optimal time offset (and optionally framerate ratio)
4. **Shifting** the subtitles by the computed offset and optionally scaling for framerate mismatches

## Why a C++ Reimplementation?

This project reimplements the [ffsubsync](https://github.com/smacke/ffsubsync) Python tool in C++. The primary motivation is to eliminate the Python runtime dependency so it can be deployed on Android, iOS, embedded systems, and anywhere Python is impractical.

- **Android Integration**: No Python runtime required; native C++ integrates cleanly via JNI
- **Performance**: FFT and audio processing benefit from native compilation and can leverage SIMD
- **Distribution**: Single binary/library with no Python environment to manage
- **Portability**: C++ can target iOS, embedded Linux, and desktop with the same core code

## Status

- **FFT Aligner**: Kiss FFT-based cross-correlation alignment
- **Sherpa ONNX VAD**: Silero VAD via prebuilt macOS binaries
- **SRT Parser**: Custom lightweight parser with round-trip support
- **Subtitle Speech Extraction**: Binary vector generation from subtitle timestamps
- **macOS CLI Tool**: `ffsubsync_cli` accepts `--wav` and `--subs-dir`

### Build

```bash
cmake -B build -S .
cmake --build build -j$(sysctl -n hw.ncpu)
ctest --test-dir build --output-on-failure
```

### CLI Usage

```bash
# Convert video to 16kHz mono WAV first
ffmpeg -i input.mp4 -ar 16000 -ac 1 -c:a pcm_s16le output.wav

# Run sync against a directory of SRT files
./build/ffsubsync_cli \
  --wav output.wav \
  --subs-dir /path/to/subtitles/ \
  --model models/silero_vad.onnx
```

Example output:
```
Audio: 321916 frames (3219.16s @ 100Hz)
The.Copenhagen.Test.S01E03.1080p.WEB.h264-ETHEL.srt -> offset: 0.42s, score: 260118
The.Copenhagen.Test.S01E03.1080p.10bit.WEBRip.6CH.x265.HEVC-PSA.srt -> offset: -0.07s, score: 256168
```

## Files in This Directory

| File | Purpose |
|------|---------|
| `README.md` | This file — high-level overview and quick start |
| `CMakeLists.txt` | Build configuration |
| `docs/ARCHITECTURE.md` | Component-by-component mapping from Python to C++ |
| `docs/DEPENDENCIES.md` | Dependency analysis and C++ equivalents |
| `docs/API_DESIGN.md` | Public C++ API and class structure |
| `docs/IMPLEMENTATION_ROADMAP.md` | Phased implementation plan with milestones |
| `docs/TESTING_STRATEGY.md` | How to verify correctness |
| `docs/CODE_MAP.md` | Detailed line-by-line mapping of Python modules to C++ |
| `docs/ADR/001-vad-dependency.md` | Architecture Decision Record: VAD dependency choice |

## Quick Reference: Python -> C++ Mapping

| Python Module | C++ Component | Notes |
|--------------|-------------|-------|
| `aligners.py` | `src/aligners/` | FFTAligner, MaxScoreAligner |
| `speech_transformers.py` | `src/speech/` | VADProcessor (Sherpa ONNX), subtitle speech extraction |
| `subtitle_parser.py` | `src/subtitles/` | SRT/ASS/VTT parsers |
| `subtitle_transformers.py` | `src/subtitles/` | Shifter, Scaler, Merger |
| `generic_subtitles.py` | `src/subtitles/` | SubtitleEntry, SRTData, ASSData |
| `ffmpeg_utils.py` | `src/media/` | FFmpeg C API wrapper (Phase 2) |
| `golden_section_search.py` | `src/aligners/` | Golden-section search for framerate ratio |
| `sklearn_shim.py` | `src/core/` | Pipeline/Transformer pattern (simplified) |
| `ffsubsync.py` | `src/cli/` | CLI application + main orchestration |
| `constants.py` | `include/ffsubsync/constants.h` | Compile-time constants |

## Key Dependencies

| Python Dependency | C++ Equivalent | Android Feasibility |
|-------------------|---------------|---------------------|
| `ffmpeg-python` + `ffmpeg` | `libavformat`, `libavcodec`, `libavutil`, `libswresample` (Phase 2) | Excellent |
| `webrtcvad` | **Sherpa ONNX + Silero VAD** | Prebuilt AAR available |
| `numpy` (FFT) | **Kiss FFT** (vendored) | Trivial |
| `srt` (parser) | Custom C++ parser | N/A |
| `pysubs2` (ASS/SSA) | Custom C++ parser (Phase 2) | Moderate |
| `argparse` | `cxxopts` | N/A |
| `torch` + `silero-vad` | **Sherpa ONNX** (same models, native runtime) | Supported |

## Project Structure

```
ffsubsync-cpp/
├── CMakeLists.txt
├── include/ffsubsync/
│   ├── constants.h
│   ├── types.h
│   ├── aligner.h
│   ├── srt_parser.h
│   ├── subtitle_speech.h
│   └── vad_processor.h
├── src/
│   ├── core/pipeline.cpp
│   ├── aligners/
│   │   ├── fft_aligner.cpp
│   │   ├── max_score_aligner.cpp
│   │   └── golden_section_search.cpp
│   ├── speech/
│   │   ├── subtitle_speech.cpp
│   │   └── vad_processor.cpp
│   ├── subtitles/
│   │   ├── srt_parser.cpp
│   │   └── subtitle_transformer.cpp
│   └── cli/main.cpp
├── tests/
│   ├── test_fft_aligner.cpp
│   ├── test_subtitle_speech.cpp
│   ├── test_srt_parser.cpp
│   ├── test_vad_processor.cpp
│   └── main.cpp
├── models/
│   └── silero_vad.onnx
├── sherpa-onnx-v1.13.2-osx-arm64-jni/
│   ├── include/sherpa-onnx/c-api/
│   └── lib/
├── test_data/
│   ├── the.copenhagen.test.s01e03/
│   └── la.brea.s01e01/
└── third_party/kiss_fft/
```

## Build Requirements

- CMake 3.18+
- C++17 compiler (Apple Clang 17+, GCC 11+, MSVC 2022+)
- macOS: prebuilt Sherpa ONNX binaries included for arm64
- Linux/Windows: download corresponding Sherpa ONNX release
- Optional: ffmpeg (for WAV conversion from video)

## Project Phases

### Phase 1: Core Library + macOS CLI ✅ COMPLETE
- FFT-based aligner (`FFTAligner`, `MaxScoreAligner`)
- SRT parser and subtitle transformers (shifter, scaler)
- Sherpa ONNX VAD integration with Silero model
- macOS CLI tool that can sync SRT against WAV
- All tests passing (13/13)

### Phase 2: Media Pipeline 🔄 IN PROGRESS
- ASS/SSA/VTT support
- Framerate ratio inference and golden-section search
- Subtitle merging
- Serialized speech cache (`.npz` equivalent)

### Phase 3: Advanced Features ⏳ PENDING
- Full CLI with video input support
- Encoding detection
- Progress callbacks and logging
- Subtitle writing with proper formatting

### Phase 4: Android Integration ⏳ PENDING
- JNI bindings
- Gradle/AAR packaging
- Android-specific FFmpeg build (minimal features)
- Java/Kotlin wrapper API
- Android UI demo app

## Critical Design Decisions

1. **FFT Library**: Kiss FFT (simple, permissive, vendored) — can upgrade to KFR later
2. **VAD**: Sherpa ONNX + Silero VAD (superior accuracy, future ASR path)
3. **Encoding Detection**: uchardet vs ICU vs minimal built-in detection (Phase 2)
4. **ASS/SSA Support**: Full `libass` integration vs lightweight custom parser (Phase 2)
5. **Memory Model**: Streaming audio processing (chunked) vs loading entire audio
6. **Error Handling**: Exceptions in C++ API, return codes in C API boundary

See `docs/IMPLEMENTATION_ROADMAP.md` for detailed phase breakdown, and `docs/API_DESIGN.md` for the proposed C++ interface.
