# Android Integration Notes

This document covers Android-specific considerations for building and integrating ffsubsync-cpp into an Android application.

---

## Architecture Overview

```
Android App (Kotlin/Java)
    |
    | JNI calls
    v
libffsubsync_android.so (C++ via NDK)
    |
    | links statically/dynamically
    v
+---------------+  +---------------+  +---------------+
|  libffsubsync |  |   libffmpeg   |  |  libwebrtc_vad|
|    (core)     |  |   (libav*)    |  |    (VAD)      |
+---------------+  +---------------+  +---------------+
    |                    |                  |
    | uses               | uses             |
    v                    v                  v
+---------------+  +---------------+  +---------------+
|     KFR       |  |  libswresample|  |    (none)     |
|  (FFT/DFT)    |  |  (resampling) |  |               |
+---------------+  +---------------+  +---------------+
```

---

## Build Configuration

### CMake for Android

```cmake
# android/CMakeLists.txt

cmake_minimum_required(VERSION 3.18.1)
project("ffsubsync-android")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific: ensure position-independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Android NDK-provided libraries
find_library(android_log log)

# Fetch dependencies
include(FetchContent)
FetchContent_Declare(kfr GIT_REPOSITORY https://github.com/kfrlib/kfr.git GIT_TAG main)
FetchContent_MakeAvailable(kfr)

# FFmpeg: use prefab or prebuilt
find_package(ffmpeg REQUIRED CONFIG)

# WebRTC VAD: vendored
add_subdirectory(third_party/webrtc_vad)

# Core library (shared for Android)
add_library(ffsubsync_core SHARED
    src/core/pipeline.cpp
    src/aligners/fft_aligner.cpp
    src/speech/webrtc_vad.cpp
    src/subtitles/srt_parser.cpp
    src/media/ffmpeg_wrapper.cpp
    # ... other sources
)

target_link_libraries(ffsubsync_core
    kfr
    webrtc_vad
    ffmpeg::avformat ffmpeg::avcodec ffmpeg::avutil ffmpeg::swresample
    ${android_log}
)

# JNI wrapper
add_library(ffsubsync_jni SHARED
    android/jni/ffsubsync_jni.cpp
)

target_link_libraries(ffsubsync_jni
    ffsubsync_core
    ${android_log}
)
```

### build.gradle Configuration

```gradle
// android/app/build.gradle
android {
    ndkVersion "25.1.8937393"
    
    defaultConfig {
        minSdk 21
        targetSdk 34
        
        externalNativeBuild {
            cmake {
                cppFlags "-O3 -fexceptions -frtti"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
        
        ndk {
            abiFilters 'arm64-v8a', 'x86_64'
        }
    }
    
    externalNativeBuild {
        cmake {
            path "CMakeLists.txt"
        }
    }
}

dependencies {
    implementation 'com.android.ndk.thirdparty:ffmpeg:1.0.0'  // or your prefab
}
```

---

## FFmpeg for Android

### Option A: Prebuilt Binaries (Recommended)

Use [tanersener/mobile-ffmpeg](https://github.com/tanersener/mobile-ffmpeg) or [arthenica/ffmpeg-kit](https://github.com/arthenica/ffmpeg-kit) prebuilt AARs.

**Pros**: Zero build time, well-tested
**Cons**: Larger APK size, may include unnecessary codecs

### Option B: Custom Minimal Build

Build FFmpeg with only audio decoders and demuxers needed for subtitle sync:

```bash
# scripts/build_ffmpeg_android.sh

PLATFORMS=("arm64-v8a" "x86_64")
API_LEVEL=21

for PLATFORM in "${PLATFORMS[@]}"; do
    case $PLATFORM in
        arm64-v8a)
            ARCH=aarch64
            CPU=armv8-a
            CROSS_PREFIX=aarch64-linux-android
            ;;
        x86_64)
            ARCH=x86_64
            CPU=x86-64
            CROSS_PREFIX=x86_64-linux-android
            ;;
    esac
    
    ./configure \
        --prefix=$(pwd)/build/$PLATFORM \
        --enable-cross-compile \
        --target-os=android \
        --arch=$ARCH \
        --cpu=$CPU \
        --sysroot=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        --cc=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${CROSS_PREFIX}${API_LEVEL}-clang \
        --cxx=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/${CROSS_PREFIX}${API_LEVEL}-clang++ \
        --disable-programs \
        --disable-doc \
        --disable-network \
        --disable-zlib \
        --disable-bzlib \
        --disable-iconv \
        --disable-shared \
        --enable-static \
        --enable-decoder=aac,mp3,ac3,eac3,flac,pcm_s16le,pcm_s24le,vorbis,opus \
        --enable-demuxer=mov,mkv,avi,mp3,wav,flac,ogg,mp4,m4a,webm \
        --enable-protocol=file
    
    make -j$(nproc)
    make install
done
```

This minimal build is ~2-3MB per ABI vs 10-20MB for full FFmpeg.

---

## JNI Design

### Threading Model

FFsubsync operations are CPU-bound and should run on background threads:

```kotlin
class FfsubsyncSyncer {
    private val scope = CoroutineScope(Dispatchers.IO + Job())
    
    fun syncAsync(reference: File, subtitle: File, listener: SyncListener) {
        scope.launch {
            try {
                val result = nativeSync(reference.path, subtitle.path)
                withContext(Dispatchers.Main) {
                    listener.onComplete(result)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    listener.onError(e.message ?: "Unknown error")
                }
            }
        }
    }
    
    fun cancel() {
        scope.cancel()
        nativeCancel()
    }
}
```

### Progress Callbacks

Progress callbacks from native code to Java require attaching to the JVM thread:

```cpp
// android/jni/ffsubsync_jni.cpp

static JavaVM* g_jvm = nullptr;
static jclass g_listener_class = nullptr;
static jmethodID g_on_progress = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    g_listener_class = env->FindClass("com/example/ffsubsync/SyncListener");
    g_on_progress = env->GetMethodID(g_listener_class, "onProgress", "(Ljava/lang/String;D)V");
    return JNI_VERSION_1_6;
}

void android_progress_callback(void* userdata, const char* stage, double fraction) {
    auto* env = static_cast<JNIEnv*>(userdata);
    // If called from non-Java thread, attach first
    JNIEnv* thread_env = env;
    int detach = 0;
    if (g_jvm->GetEnv((void**)&thread_env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&thread_env, nullptr);
        detach = 1;
    }
    
    jstring jstage = thread_env->NewStringUTF(stage);
    thread_env->CallVoidMethod(g_listener, g_on_progress, jstage, fraction);
    thread_env->DeleteLocalRef(jstage);
    
    if (detach) {
        g_jvm->DetachCurrentThread();
    }
}
```

### String Encoding

All strings crossing JNI must be UTF-8:

```cpp
// Java String -> C++ string
jstring jpath = ...;
const char* cpath = env->GetStringUTFChars(jpath, nullptr);
std::string path(cpath);
env->ReleaseStringUTFChars(jpath, cpath);

// C++ string -> Java String
jstring jresult = env->NewStringUTF(result.c_str());
```

---

## Memory Considerations

### Heap Limits
Android apps have limited heap (varies by device, typically 192MB-512MB). FFsubsync must:

1. **Use chunked audio extraction** by default
   ```cpp
   extractor.extract_chunked(path, config, [](const int16_t* data, size_t samples) {
       vad.process_chunk(data, samples);
       return true;  // continue
   });
   ```

2. **Avoid loading entire video into memory**
   FFmpeg's `av_read_frame` + `avcodec_receive_frame` naturally streams

3. **Process speech vectors incrementally**
   Instead of `std::vector<float>` for entire video, consider `std::deque` or streaming FFT

### Native Memory
Native (C++) memory is separate from Java heap and less constrained, but still track with:
```cpp
// Use custom allocator for large buffers to track usage
template<typename T>
struct TrackedAllocator {
    T* allocate(size_t n) {
        T* ptr = std::allocator<T>().allocate(n);
        g_native_memory_used += n * sizeof(T);
        return ptr;
    }
    // ... deallocate
};
```

---

## Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Sync time (1hr video) | < 30s | `time ./ffsubsync video.mp4 -i subs.srt` |
| Memory peak | < 200MB | Android Profiler |
| APK size increase | < 5MB | Per ABI with minimal FFmpeg |
| Battery impact | Minimal | Avoid keeping CPU at 100% for long periods |

---

## Permissions

Android app needs:
```xml
<!-- For reading video/subtitle files -->
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
<uses-permission android:name="android.permission.READ_MEDIA_VIDEO" />

<!-- For writing synced subtitles -->
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
```

With Android 10+ scoped storage, use `SAF` (Storage Access Framework) to let users pick files:
```kotlin
val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
    addCategory(Intent.CATEGORY_OPENABLE)
    type = "*/*"
    putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("video/*", "application/x-subrip"))
}
startActivityForResult(intent, REQUEST_CODE_PICK_FILES)
```

---

## Testing on Android

### Emulator Tests
```bash
# Build test APK
./gradlew connectedAndroidTest

# Run specific test
adb shell am instrument -w -e class com.example.ffsubsync.FFsubsyncTest \
    com.example.ffsubsync.test/androidx.test.runner.AndroidJUnitRunner
```

### Device Testing Checklist
- [ ] Low-end device (2GB RAM): No OOM crashes
- [ ] ARM64 device: Correct ABI loading
- [ ] x86_64 emulator (if targeting): Works correctly
- [ ] Large video file (> 2GB): Handles correctly
- [ ] Network path (NAS/SMB): Error handling
- [ ] Background sync: Works with Doze mode
- [ ] Screen rotation: Async task survives
- [ ] Cancellation: User can cancel mid-sync

---

## ProGuard / R8 Rules

If using minification, keep JNI classes:
```proguard
-keep class com.example.ffsubsync.FFsubsync {
    native <methods>;
}
-keepclasseswithmembernames class * {
    native <methods>;
}
```

---

## Debugging Native Code

### Android Studio
1. Enable C++ debugging in `build.gradle`:
   ```gradle
   android {
       buildTypes {
           debug {
               externalNativeBuild {
                   cmake {
                       arguments "-DCMAKE_BUILD_TYPE=Debug"
                   }
               }
           }
       }
   }
   ```

2. Use LLDB breakpoints in C++ code
3. Inspect memory with Android Memory Profiler

### Logging
Use Android log from C++:
```cpp
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Ffsubsync", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Ffsubsync", __VA_ARGS__)

LOGE("Failed to open file: %s", path.c_str());
```

---

## Distribution

### AAR Structure
```
ffsubsync-android.aar
├── AndroidManifest.xml
├── classes.jar
├── jni/
│   ├── arm64-v8a/
│   │   ├── libffsubsync_jni.so
│   │   ├── libffsubsync_core.so
│   │   └── libavcodec.so (or static)
│   └── x86_64/
│       └── ...
└── assets/
```

### Maven Publication
```gradle
publishing {
    publications {
        release(MavenPublication) {
            groupId = 'com.github.yourusername'
            artifactId = 'ffsubsync-android'
            version = '1.0.0'
            artifact("$buildDir/outputs/aar/${project.name}-release.aar")
        }
    }
}
```

---

## Known Android-Specific Issues

1. **FFmpeg dynamic linking on Android < 6.0**: May need to bundle `libc++_shared.so`
2. **File paths with non-ASCII characters**: Use `std::filesystem::u8path` and UTF-8 consistently
3. **Background execution limits**: Use `WorkManager` for long sync operations
4. **Text encoding in subtitles**: Android's default charset is UTF-8; handle legacy encodings carefully
