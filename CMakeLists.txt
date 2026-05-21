cmake_minimum_required(VERSION 3.18)
project(ffsubsync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(FFSUBSYNC_BUILD_TESTS "Build tests" ON)
option(FFSUBSYNC_BUILD_CLI "Build CLI tool" ON)
option(FFSUBSYNC_BUILD_ANDROID "Build for Android" OFF)

# Kiss FFT (vendored)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/kiss_fft)

# Sherpa ONNX (prebuilt macOS binaries)
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

# Set RPATH so the executable can find dylibs at runtime
set(CMAKE_BUILD_RPATH ${SHERPA_ONNX_LIB_DIR})
set(CMAKE_INSTALL_RPATH ${SHERPA_ONNX_LIB_DIR})
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# spdlog (FetchContent)
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# Core library
add_library(ffsubsync_core STATIC
    src/core/pipeline.cpp
    src/aligners/fft_aligner.cpp
    src/aligners/max_score_aligner.cpp
    src/aligners/golden_section_search.cpp
    src/speech/subtitle_speech.cpp
    src/speech/vad_processor.cpp
    src/subtitles/srt_parser.cpp
    src/subtitles/subtitle_transformer.cpp
)

target_include_directories(ffsubsync_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ffsubsync_core
    PUBLIC
        kissfft::kissfft
        sherpa-onnx-cxx-api
        onnxruntime
        spdlog::spdlog
)

# CLI tool
if(FFSUBSYNC_BUILD_CLI)
    FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
        GIT_TAG v3.1.1
    )
    FetchContent_MakeAvailable(cxxopts)

    add_executable(ffsubsync_cli
        src/cli/main.cpp
    )
    target_link_libraries(ffsubsync_cli
        PRIVATE
            ffsubsync_core
            cxxopts::cxxopts
    )
endif()

# Tests
if(FFSUBSYNC_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(ffsubsync_tests
        tests/test_fft_aligner.cpp
        tests/test_subtitle_speech.cpp
        tests/test_srt_parser.cpp
        tests/test_vad_processor.cpp
        tests/main.cpp
    )
    target_link_libraries(ffsubsync_tests
        PRIVATE
            ffsubsync_core
            Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(ffsubsync_tests)
endif()
