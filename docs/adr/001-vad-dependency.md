# ADR 001: Voice Activity Detection (VAD) Dependency

## Status
**Superseded** — Sherpa ONNX VAD is now the primary engine. libfvad support has been removed.

## Context
FFsubsync C++ needs Voice Activity Detection (VAD) to convert audio streams into binary speech arrays (1 = speech, 0 = silence), matching the Python implementation's use of `webrtcvad`.

### Historical Options Evaluated

#### Option A: Full WebRTC build
- Clone `webrtc-sdk/webrtc` (~150MB) and build with GN/Ninja
- Pro: Always up-to-date, official code
- Con: Massive build system complexity, pulls in Abseil, rtc_base, etc.
- Verdict: Overkill for just VAD

#### Option B: Cherry-pick from webrtc-sdk/webrtc via CMake FetchContent
- Fetch the repo, compile only `common_audio/vad/` + `common_audio/signal_processing/`
- Pro: Close to upstream, easy updates by changing GIT_TAG
- Con: Requires stubbing `rtc_base/checks.h`, `rtc_base/sanitizer.h`, handling architecture macros, and one C++ file (`dot_product_with_scale.cc`). Transitive dependencies make this more complex than it appears.
- Verdict: Doable but requires ongoing maintenance of stubs

#### Option C: libfvad (Original MVP Choice)
- Standalone fork of WebRTC VAD with stubs already in place (~588 stars)
- Pro: Clean C API (`fvad.h`), no Abseil or rtc_base dependencies, trivial CMake integration
- Con: Last updated ~2 years ago; WebRTC VAD has had updates in the last 9 months. Accuracy inferior to neural VAD.
- Verdict: **Used for initial MVP, now removed**

#### Option D: Sherpa ONNX + Silero VAD (Current)
- Prebuilt macOS binaries with C++ API (`sherpa-onnx/c-api/cxx-api.h`)
- Pro: Superior accuracy (neural vs heuristic), same binary size footprint (~600KB model), opens path to future ASR features (STT validation, ad-break detection)
- Con: Requires ONNX Runtime dependency (~15MB), model file must be shipped or downloaded
- Verdict: **Accepted**

## Decision
Use **Sherpa ONNX with Silero VAD** as the VAD dependency.

## Rationale
1. **Accuracy**: Silero VAD significantly outperforms WebRTC VAD on real-world content (dialogue, music, noise)
2. **Future-proofing**: Sherpa ONNX opens the door to ASR-based features (binary-search ad-break detection, STT validation) without adding new dependencies
3. **Clean API**: The `sherpa_onnx::cxx::VoiceActivityDetector` RAII wrapper is idiomatic C++ with automatic cleanup
4. **Platform support**: Prebuilt binaries available for macOS (arm64/x64), Linux, Windows, Android

## Consequences
- ✅ Superior VAD accuracy on real content
- ✅ Neural model can be updated independently of code
- ✅ Shared ONNX Runtime can be reused for future ASR features
- ⚠️ ONNX Runtime binary is ~15MB (acceptable for desktop; may require split APK on Android)
- ⚠️ Model file (`silero_vad.onnx`, ~600KB) must be bundled or fetched at runtime
- ⚠️ Requires 16kHz mono input (WebRTC supported 8/16/32/48kHz)

## Integration Details

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

### API Usage
```cpp
#include "sherpa-onnx/c-api/cxx-api.h"

sherpa_onnx::cxx::VadModelConfig config;
config.silero_vad.model = "models/silero_vad.onnx";
config.silero_vad.threshold = 0.5f;
config.sample_rate = 16000;

auto vad = sherpa_onnx::cxx::VoiceActivityDetector::Create(config, 60.0f);
vad.AcceptWaveform(samples.data(), samples.size());
vad.Flush();

while (!vad.IsEmpty()) {
    auto segment = vad.Front();
    // segment.start = sample index
    // segment.samples = float PCM
    vad.Pop();
}
```

## Related Files
- `CMakeLists.txt`
- `src/speech/vad_processor.cpp`
- `include/ffsubsync/vad_processor.h`
- `models/silero_vad.onnx`
