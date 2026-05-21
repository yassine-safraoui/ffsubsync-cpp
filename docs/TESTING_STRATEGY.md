# Testing Strategy

This document outlines how to verify that the C++ implementation produces correct results compared to the Python reference implementation.

---

## Testing Philosophy

The core algorithm (FFT cross-correlation) is deterministic and should produce **numerically identical** results given the same inputs. However, due to:
- Different FFT implementations (numpy vs Kiss FFT)
- Floating-point ordering differences
- VAD behavior differences (Sherpa ONNX Silero vs WebRTC)

We define three levels of correctness:

1. **Bit-exact**: Identical output to Python (ideal but not always achievable)
2. **Equivalent**: Same alignment offset, score within 0.1%
3. **Acceptable**: Offset within 1 sample (10ms at 100Hz)

---

## Unit Tests

### FFT Aligner Tests

Port `tests/test_alignment.py`:

```cpp
// tests/test_fft_aligner.cpp
#include <catch2/catch_test_macros.hpp>
#include <ffsubsync/aligner.h>

TEST_CASE("FFTAligner basic alignment", "[aligner]") {
    ffsubsync::FFTAligner aligner;
    
    SECTION("simple offset -1") {
        std::vector<float> ref = {1, 1, 1, 0, 0, 1};  // "111001"
        std::vector<float> sub = {1, 1, 0, 0, 1};      // "11001"
        auto result = aligner.align(ref, sub);
        REQUIRE(result.offset_samples == -1);
    }
    
    SECTION("no offset") {
        std::vector<float> ref = {1, 0, 0, 1};
        std::vector<float> sub = {1, 0, 0, 1};
        auto result = aligner.align(ref, sub);
        REQUIRE(result.offset_samples == 0);
    }
    
    SECTION("offset +1") {
        std::vector<float> ref = {1, 0, 0, 1, 0};
        std::vector<float> sub = {0, 1, 0, 0, 1};
        auto result = aligner.align(ref, sub);
        REQUIRE(result.offset_samples == 1);
    }
}
```

### Subtitle Speech Extraction Tests

Port `tests/test_subtitles.py`:

```cpp
// tests/test_subtitle_speech.cpp
#include <catch2/catch_test_macros.hpp>
#include <ffsubsync/subtitles.h>
#include <ffsubsync/speech.h>

const char* fake_srt = R"(1
00:00:00,178 --> 00:00:01,416
<i>Previously on...</i>

2
00:00:01,828 --> 00:00:04,549
Oh hi, Mark.

3
00:00:04,653 --> 00:00:06,062
You are tearing me apart!
)";

TEST_CASE("Subtitle speech extraction", "[speech]") {
    auto parser = ffsubsync::make_parser(ffsubsync::SubtitleFormat::SRT);
    auto subs = parser->parse_string(fake_srt);
    
    ffsubsync::SubtitleSpeechTransformer extractor(100);  // 100 Hz
    auto result = extractor.extract_speech(subs);
    
    SECTION("correct number of frames") {
        // 6.062 seconds * 100 Hz = 607 frames
        REQUIRE(result.speech_frames.size() == 607);
    }
    
    SECTION("speech at correct positions") {
        // First subtitle: 0.178s to 1.416s
        int start = 18;   // 0.178 * 100
        int end = 142;    // 1.416 * 100
        for (int i = start; i < end; ++i) {
            REQUIRE(result.speech_frames[i] > 0.5f);
        }
    }
}
```

### SRT Parser Tests

```cpp
// tests/test_srt_parser.cpp
TEST_CASE("SRT parser round-trip", "[parser]") {
    auto parser = std::make_unique<ffsubsync::SRTParser>();
    auto subs = parser->parse_string(fake_srt);
    
    REQUIRE(subs.size() == 3);
    REQUIRE(subs[0].start.count() == 178);   // 00:00:00,178
    REQUIRE(subs[0].end.count() == 1416);    // 00:00:01,416
    REQUIRE(subs[0].content == "Previously on...");
}
```

### VAD Processor Tests

```cpp
// tests/test_vad_processor.cpp
TEST_CASE("VADProcessor construction succeeds with valid model", "[vad]") {
    ffsubsync::VADProcessor vad("models/silero_vad.onnx", 16000);
    REQUIRE(true);
}

TEST_CASE("VADProcessor construction fails with invalid model", "[vad]") {
    REQUIRE_THROWS(ffsubsync::VADProcessor("/nonexistent/model.onnx", 16000));
}

TEST_CASE("VADProcessor rejects unsupported sample rate", "[vad]") {
    REQUIRE_THROWS(ffsubsync::VADProcessor("models/silero_vad.onnx", 48000));
}

TEST_CASE("VADProcessor process returns correctly sized vector", "[vad]") {
    ffsubsync::VADProcessor vad("models/silero_vad.onnx", 16000);
    
    // 2 seconds of mixed signal
    auto speech = generate_sine_burst(16000, 440.0f, 1000);
    auto silence = generate_silence(16000, 1000);
    std::vector<float> combined;
    combined.insert(combined.end(), speech.begin(), speech.end());
    combined.insert(combined.end(), silence.begin(), silence.end());
    
    auto result = vad.process(combined, 100.0f);
    
    // 2 seconds @ 100 Hz = 200 samples
    REQUIRE(result.size() == 200);
    
    // All values must be exactly 0.0 or 1.0
    for (float v : result) {
        REQUIRE((v == 0.0f || v == 1.0f));
    }
}
```

---

## Integration Tests

### End-to-End Sync Tests

Create test fixtures with known offsets:

```cpp
// tests/test_end_to_end.cpp
TEST_CASE("Sync SRT against video", "[integration]") {
    // Reference: video with known speech pattern
    // Subtitle: manually created with +2.5s offset
    
    ffsubsync::SyncOptions opts;
    opts.reference_path = "test-data/video_with_speech.mp4";
    opts.input_subtitles = {"test-data/subs_offset_2.5s.srt"};
    opts.output_path = "test-output/synced.srt";
    
    auto results = ffsubsync::synchronize(opts);
    
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].success);
    REQUIRE(results[0].offset_seconds == Approx(2.5).margin(0.1));
}
```

### CLI Integration Tests

Test the CLI against real test data:

```bash
# Copenhagen Test S01E03
./ffsubsync_cli \
  --wav test_data/the.copenhagen.test.s01e03/converted_16k_mono.wav \
  --subs-dir test_data/the.copenhagen.test.s01e03/subtitles/ \
  --model models/silero_vad.onnx

# Expected: offsets ~0.4s for most subs, near-perfect sync

# La Brea S01E01
./ffsubsync_cli \
  --wav test_data/la.brea.s01e01/converted_16k_mono.wav \
  --subs-dir test_data/la.brea.s01e01/subtitles/ \
  --model models/silero_vad.onnx

# Expected: two clusters at ~+3s (AMZN) and ~+7s (GOSSIP/ION10)
```

### Python vs C++ Comparison Test

Write a Python script that:
1. Runs Python ffsubsync on test cases
2. Runs C++ ffsubsync on same test cases
3. Compares outputs

```python
# tests/compare_with_python.py
import subprocess
import sys

def test_case(video, subtitle, expected_offset):
    # Run Python version
    py_result = subprocess.run(
        [sys.executable, "-m", "ffsubsync", video, "-i", subtitle, "-o", "/tmp/py_out.srt"],
        capture_output=True, text=True
    )
    
    # Run C++ version
    cpp_result = subprocess.run(
        ["./build/ffsubsync", video, "-i", subtitle, "-o", "/tmp/cpp_out.srt"],
        capture_output=True, text=True
    )
    
    # Compare outputs
    with open("/tmp/py_out.srt") as f:
        py_out = f.read()
    with open("/tmp/cpp_out.srt") as f:
        cpp_out = f.read()
    
    assert py_out == cpp_out, "Outputs differ!"
```

---

## Regression Test Suite

Maintain a set of "golden" test cases:

```
test-data/
├── case-1-simple-offset/
│   ├── reference.mp4
│   ├── input.srt
│   ├── expected_output.srt
│   └── expected_offset.txt
├── case-2-framerate-mismatch/
│   ├── reference.mp4
│   ├── input.srt
│   └── expected_ratio.txt
├── case-3-utf8-encoding/
│   ├── reference.mp4
│   ├── input.srt
│   └── expected_output.srt
└── case-4-empty-audio/
    ├── reference.mp4
    ├── input.srt
    └── expected_failure.txt
```

Each case has an `expected_*.txt` with the Python reference output.

CI runs:
```bash
# 1. Build C++ version
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 2. Run Python reference on all cases
python scripts/generate_expected.py test-data/

# 3. Run C++ version on all cases and compare
python scripts/compare_outputs.py test-data/ --cpp-binary ./build/ffsubsync
```

---

## Performance Benchmarks

Track performance vs Python:

```cpp
// tests/bench_sync.cpp
#include <chrono>

TEST_CASE("Benchmark sync performance", "[bench][.]") {
    auto start = std::chrono::high_resolution_clock::now();
    
    ffsubsync::SyncOptions opts;
    opts.reference_path = "test-data/long_movie.mp4";
    opts.input_subtitles = {"test-data/subs.srt"};
    auto results = ffsubsync::synchronize(opts);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Sync took " << ms << "ms\n";
    
    // Python typically takes 20-30s for a 1-hour video
    // C++ should be significantly faster
    REQUIRE(ms < 15000);  // Should be under 15s
}
```

---

## Android-Specific Tests

### JNI Tests
```kotlin
// android/src/test/java/.../FFsubsyncUnitTest.kt
@Test
fun testSyncSRT() {
    val syncer = FfsubsyncSyncer()
    syncer.setReference(testVideoFile.absolutePath)
    syncer.addInput(testSubtitleFile.absolutePath)
    
    val result = syncer.sync()
    
    assertTrue(result.success)
    assertEquals(2.5, result.offsetSeconds, 0.1)
}
```

### Instrumentation Tests
```kotlin
// android/src/androidTest/java/.../FFsubsyncInstrumentedTest.kt
@Test
fun testSyncLargeFile() {
    val latch = CountDownLatch(1)
    var success = false
    
    Ffsubsync.syncAsync(largeVideoFile, subtitleFile, object : SyncListener {
        override fun onProgress(stage: String, percent: Int) {
            assertTrue(percent in 0..100)
        }
        override fun onComplete(result: SyncResult) {
            success = result.success
            latch.countDown()
        }
        override fun onError(error: String) {
            latch.countDown()
        }
    })
    
    assertTrue(latch.await(60, TimeUnit.SECONDS))
    assertTrue(success)
}
```

---

## Test Data Strategy

1. **Synthetic**: Programmatically generate SRT files with known offsets
2. **Public domain media**: Use Creative Commons videos from archive.org
3. **Real fixtures**: Keep test data in `test_data/` directory
4. **Minimal fixtures**: Keep unit test data small (< 1MB) for fast CI
5. **Large fixtures**: Optional separate repo for long-running integration tests

### Current Test Data
```
test_data/
├── the.copenhagen.test.s01e03/
│   ├── converted_16k_mono.wav
│   └── subtitles/
│       ├── The.Copenhagen.Test.S01E03.1080p.HEVC.x265-MeGusta[EZTVx.to].srt
│       ├── The.Copenhagen.Test.S01E03.1080p.WEB.h264-ETHEL.srt
│       ├── The.Copenhagen.Test.S01E03.1080p.10bit.WEBRip.6CH.x265.HEVC-PSA.srt
│       └── The.Copenhagen.Test.S01E03.1080p.WEB.h264-ETHEL.Hi.srt
└── la.brea.s01e01/
    ├── converted_16k_mono.wav
    └── subtitles/
        ├── La.Brea.S01E01.WEBRip.x264-ION10.eng.srt
        ├── La.Brea.S01E01.1080p.AMZN.WEB-DL.DDP5.1.H.264-TOMMY.AR.srt
        ├── La.Brea.S01E01.1080p.AMZN.WEB-DL.DDP5.1.H.264-TOMMY.srt
        └── ... (11 subtitle files total)
```

---

## CI/CD Test Matrix

| Platform | Build | Unit Tests | Integration | Android Tests |
|----------|-------|------------|-------------|---------------|
| Ubuntu 22.04 | ✅ | ✅ | ✅ | - |
| macOS 13 | ✅ | ✅ | ✅ | - |
| Windows 2022 | ✅ | ✅ | - | - |
| Android Emulator (API 30) | - | - | - | ✅ |
| Android Device (ARM64) | - | - | - | ✅ (nightly) |
