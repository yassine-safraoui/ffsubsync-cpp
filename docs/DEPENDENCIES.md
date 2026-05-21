# Dependency Analysis and C++ Equivalents

This document maps every significant dependency in the Python project to a C++ equivalent, with a focus on desktop (macOS/Linux/Windows) and future Android compatibility.

---

## Runtime Dependencies (from `requirements.txt`)

| Python Package | Purpose | C++ Replacement | License | Android Build |
|----------------|---------|-------------------|---------|---------------|
| `ffmpeg-python` + `ffmpeg` | Video/audio demuxing, decoding, resampling | `libavformat`, `libavcodec`, `libavutil`, `libswresample` (or ffmpeg CLI) | LGPL 2.1+ / GPL 2+ | Excellent — official NDK support via prebuilt or source |
| `numpy` | FFT, array operations, numerical computing | **Kiss FFT** (vendored) | BSD | Trivial — single header + C file |
| `webrtcvad` / `webrtcvad-wheels` | Voice Activity Detection | **Sherpa ONNX + Silero VAD** | ONNX: MIT, Silero: MIT | Prebuilt AAR available; NDK build supported |
| `srt` | SRT subtitle parsing | **Custom parser** | N/A | N/A |
| `pysubs2` | ASS/SSA/VTT subtitle parsing | **Custom parser** or `libass` (partial) | libass: ISC | Moderate |
| `chardet` / `cchardet` / `charset_normalizer` | Character encoding detection | **uchardet** (Mozilla) or **ICU** | uchardet: MPL/LGPL, ICU: ICU license | Both compile for Android |
| `auditok` | Alternative VAD (audio energy-based) | **Skip** | — | Not needed |
| `torch` + `silero-vad` | Neural VAD | **Sherpa ONNX** (same models, native runtime) | MIT | Complex — large binary size |
| `rich` | Terminal UI / progress bars | **fmt** + **indicators** or Android UI | fmt: MIT, indicators: MIT | N/A |
| `tqdm` | Progress bars | Custom callback or `indicators` library | N/A | N/A |

---

## FFT Library: Kiss FFT

We use **Kiss FFT** (vendored in `third_party/kiss_fft`) rather than KFR:

- **Pros**: Simple (~2000 LOC), permissive (BSD), easy to vendor, static linkable
- **Cons**: Slower than optimized libraries, no automatic padding
- **Android**: Trivial — single header + C file
- **Rationale**: Chosen for stability and minimal build complexity. Static linking avoids RPATH issues on macOS.

---

## VAD: Sherpa ONNX + Silero VAD

### Why Sherpa ONNX?
- **Accuracy**: Neural Silero VAD significantly outperforms WebRTC heuristic VAD
- **Future-proofing**: Same ONNX Runtime can power future ASR features
- **API**: Clean C++ RAII wrapper (`sherpa_onnx::cxx::VoiceActivityDetector`)
- **Platform**: Prebuilt binaries for macOS (arm64/x64), Linux, Windows, Android

### Prebuilt Binaries
```
sherpa-onnx-v1.13.2-osx-arm64-jni/
├── include/sherpa-onnx/c-api/
│   ├── c-api.h
│   └── cxx-api.h
├── lib/
│   ├── libonnxruntime.dylib
│   ├── libonnxruntime.1.24.4.dylib
│   ├── libsherpa-onnx-c-api.dylib
│   └── libsherpa-onnx-cxx-api.dylib
└── bin/             (reference binaries)
```

### Model File
- `silero_vad.onnx` (~629 KB, 16kHz-only)
- Downloaded from: https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
- Stored in: `models/silero_vad.onnx`

### CMake Integration
```cmake
set(SHERPA_ONNX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/sherpa-onnx-v1.13.2-osx-arm64-jni)
set(SHERPA_ONNX_LIB_DIR ${SHERPA_ONNX_ROOT}/lib)

add_library(sherpa-onnx-c-api SHARED IMPORTED)
set_target_properties(sherpa-onnx-c-api PROPERTIES
    IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-c-api.dylib
    INTERFACE_INCLUDE_DIRECTORIES ${SHERPA_ONNX_ROOT}/include
)

add_library(sherpa-onnx-cxx-api SHARED IMPORTED)
set_target_properties(sherpa-onnx-cxx-api PROPERTIES
    IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-cxx-api.dylib
    INTERFACE_INCLUDE_DIRECTORIES ${SHERPA_ONNX_ROOT}/include
    INTERFACE_LINK_LIBRARIES sherpa-onnx-c-api
)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libonnxruntime.1.24.4.dylib
)

set(CMAKE_BUILD_RPATH ${SHERPA_ONNX_LIB_DIR})
set(CMAKE_INSTALL_RPATH ${SHERPA_ONNX_LIB_DIR})
```

### RPATH Notes (macOS)
- macOS executables must find `.dylib` files at runtime
- `CMAKE_BUILD_RPATH` is set to the prebuilt lib directory during development
- For distribution, use `install_name_tool` or copy dylibs next to the executable

---

## Audio I/O

### Desktop (macOS / Linux)
- **FFmpeg C libraries (libav*)**: `libavformat`, `libavcodec`, `libswresample`, `libavutil`
- Acquired via system package manager (`brew install ffmpeg`, `apt install libav*-dev`)
- CMake finds libraries via `pkg-config` with `find_library()` fallback
- Pro: No subprocess, handles all formats, embedded in library
- Con: Requires FFmpeg installed as a system dependency

### Android (Phase 4)
- **FFmpeg C libraries**: Prebuilt static libraries per ABI using `ffmpeg-android-maker`
- Libraries committed to `third_party/ffmpeg/android/{abi}/`
- CMake uses `find_library()` against prebuilt paths when `FFSUBSYNC_BUILD_ANDROID=ON`
- No runtime dependency on system FFmpeg

---

## Subtitle Parsing Libraries

### SRT
No third-party library needed. Custom parser is ~150 lines.

### ASS/SSA
- **libass**: Full C renderer. Can parse but brings rendering baggage.
- **Custom parser**: Parse `[Script Info]`, `[V4+ Styles]`, `[Events]` sections. Extract `Dialogue:` lines with timing.
- **Recommendation**: Custom lightweight parser for Phase 2.

### VTT (WebVTT)
Simpler than ASS. Custom parser ~100 lines.

---

## Encoding Detection

### uchardet (Recommended)
```cmake
FetchContent_Declare(
    uchardet
    GIT_REPOSITORY https://gitlab.freedesktop.org/uchardet/uchardet.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(uchardet)
```

### ICU (Alternative)
Heavyweight but comprehensive. Only needed if we need full Unicode normalization or collation.

### Minimal Strategy
1. Try UTF-8 BOM
2. Try UTF-8 validation
3. Fall back to uchardet
4. Default to UTF-8 if detection fails

This covers >99% of modern subtitle files.

---

## CLI / Progress / Logging

| Python | C++ Replacement | Notes |
|--------|-----------------|-------|
| `argparse` | `cxxopts` | Header-only, already integrated via FetchContent |
| `logging` | `spdlog` | Fast, feature-rich, already integrated |
| `tqdm` | `indicators` or custom callbacks | indicators is header-only, modern |
| `rich` (progress bars) | Android: ProgressBar via JNI callback | Desktop: indicators |

### Progress Callback Design
```cpp
using ProgressCallback = std::function<void(const std::string& stage, double fraction)>;

// In CLI:
auto progress = [](const std::string& stage, double fraction) {
    std::cout << "[" << stage << "] " << int(fraction * 100) << "%\r" << std::flush;
};

// In Android:
auto progress = [](const std::string& stage, double fraction) {
    JNIEnv* env = ...;
    // Call Java callback
};
```

---

## Testing Dependencies

| Python | C++ |
|--------|-----|
| `pytest` | **Catch2** |
| `pytest.mark.parametrize` | `CATCH2 macros` |

**Integrated**: Catch2 v3 via FetchContent.

---

## Summary: Current Dependency Stack

| Category | Library | Version/Source |
|----------|---------|---------------|
| FFT | Kiss FFT | Vendored in `third_party/kiss_fft` |
| VAD | Sherpa ONNX + Silero VAD | Prebuilt binaries + `silero_vad.onnx` model |
| Audio I/O | FFmpeg C libraries (desktop: system pkg-config, Android: prebuilt static) | System pkg-config / `ffmpeg-android-maker` |
| SRT Parsing | Custom | In-tree |
| ASS/SSA Parsing | Custom (Phase 2) | In-tree |
| Encoding Detection | uchardet (Phase 2) | `master` via FetchContent |
| CLI Parsing | cxxopts | `v3.1.1` via FetchContent |
| Logging | spdlog | `v1.12.0` via FetchContent |
| Testing | Catch2 v3 | `v3.4.0` via FetchContent |
| String Formatting | fmt | Bundled with spdlog |

This stack is entirely permissively licensed (MIT/BSD/BSL-1.0/ISC) with the exception of FFmpeg (LGPL), which is acceptable for dynamic linking or proper compliance in Android apps.
