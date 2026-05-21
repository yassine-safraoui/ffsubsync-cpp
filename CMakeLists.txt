cmake_minimum_required(VERSION 3.18)
project(ffsubsync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(FFSUBSYNC_BUILD_TESTS "Build tests" ON)
option(FFSUBSYNC_BUILD_CLI "Build CLI tool" ON)

# FetchContent (used by Kiss FFT fallback and other deps)
include(FetchContent)

# Kiss FFT
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    kiss_fft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG master
)
FetchContent_MakeAvailable(kiss_fft)

# Sherpa ONNX
set(SHERPA_ONNX_VERSION "v1.13.2")
set(SHERPA_ONNX_DESKTOP_URL "https://github.com/k2-fsa/sherpa-onnx/releases/download/${SHERPA_ONNX_VERSION}/sherpa-onnx-${SHERPA_ONNX_VERSION}-osx-arm64-jni.tar.bz2")
set(SHERPA_ONNX_ANDROID_URL "https://github.com/k2-fsa/sherpa-onnx/releases/download/${SHERPA_ONNX_VERSION}/sherpa-onnx-${SHERPA_ONNX_VERSION}-android.tar.bz2")
set(SHERPA_ONNX_CACHE_DIR ${CMAKE_BINARY_DIR}/sherpa-onnx)

# Download macOS/desktop tarball (headers are platform-independent, libs used on macOS)
set(SHERPA_ONNX_DESKTOP_DIR ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-${SHERPA_ONNX_VERSION}-osx-arm64-jni)
if(NOT EXISTS ${SHERPA_ONNX_DESKTOP_DIR})
    message(STATUS "Downloading Sherpa ONNX desktop package...")
    file(MAKE_DIRECTORY ${SHERPA_ONNX_CACHE_DIR})
    file(DOWNLOAD
        ${SHERPA_ONNX_DESKTOP_URL}
        ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-desktop.tar.bz2
        SHOW_PROGRESS
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-desktop.tar.bz2
        WORKING_DIRECTORY ${SHERPA_ONNX_CACHE_DIR}
    )
    file(REMOVE ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-desktop.tar.bz2)
endif()

set(SHERPA_ONNX_INCLUDE_DIR ${SHERPA_ONNX_DESKTOP_DIR}/include)

if(ANDROID)
    # Download Android tarball for per-ABI .so files
    set(SHERPA_ONNX_ANDROID_DIR ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-${SHERPA_ONNX_VERSION}-android)
    if(NOT EXISTS ${SHERPA_ONNX_ANDROID_DIR})
        message(STATUS "Downloading Sherpa ONNX Android package...")
        file(MAKE_DIRECTORY ${SHERPA_ONNX_CACHE_DIR})
        file(DOWNLOAD
            ${SHERPA_ONNX_ANDROID_URL}
            ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-android.tar.bz2
            SHOW_PROGRESS
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xjf ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-android.tar.bz2
            WORKING_DIRECTORY ${SHERPA_ONNX_CACHE_DIR}
        )
        file(REMOVE ${SHERPA_ONNX_CACHE_DIR}/sherpa-onnx-android.tar.bz2)
    endif()

    set(SHERPA_ONNX_LIB_DIR ${SHERPA_ONNX_ANDROID_DIR}/${ANDROID_ABI})

    foreach(lib_name IN ITEMS sherpa-onnx-c-api sherpa-onnx-cxx-api onnxruntime)
        add_library(${lib_name} SHARED IMPORTED)
        set_target_properties(${lib_name} PROPERTIES
            IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/lib${lib_name}.so
            INTERFACE_INCLUDE_DIRECTORIES ${SHERPA_ONNX_INCLUDE_DIR}
        )
    endforeach()

    message(STATUS "Sherpa ONNX Android: using ${SHERPA_ONNX_LIB_DIR}")
elseif(APPLE)
    set(SHERPA_ONNX_LIB_DIR ${SHERPA_ONNX_DESKTOP_DIR}/lib)

    add_library(sherpa-onnx-c-api SHARED IMPORTED)
    set_target_properties(sherpa-onnx-c-api PROPERTIES
        IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-c-api.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${SHERPA_ONNX_INCLUDE_DIR}
    )

    add_library(sherpa-onnx-cxx-api SHARED IMPORTED)
    set_target_properties(sherpa-onnx-cxx-api PROPERTIES
        IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libsherpa-onnx-cxx-api.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${SHERPA_ONNX_INCLUDE_DIR}
        INTERFACE_LINK_LIBRARIES sherpa-onnx-c-api
    )

    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION ${SHERPA_ONNX_LIB_DIR}/libonnxruntime.dylib
    )

    set(CMAKE_BUILD_RPATH ${SHERPA_ONNX_LIB_DIR})
    set(CMAKE_INSTALL_RPATH ${SHERPA_ONNX_LIB_DIR})
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

    message(STATUS "Sherpa ONNX macOS: using ${SHERPA_ONNX_LIB_DIR}")
else()
    message(FATAL_ERROR "Unsupported platform. Only macOS and Android are currently supported for Sherpa ONNX.")
endif()

# # spdlog (FetchContent)
# FetchContent_Declare(
#     spdlog
#     GIT_REPOSITORY https://github.com/gabime/spdlog.git
#     GIT_TAG v1.12.0
# )
# FetchContent_MakeAvailable(spdlog)

# FFmpeg
set(FFMPEG_LIBS "")
if(NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
            libavformat>=58.0
            libavcodec>=58.0
            libswresample>=3.0
            libavutil>=56.0
        )
        if(FFMPEG_FOUND)
            set(FFMPEG_LIBS PkgConfig::FFMPEG)
            message(STATUS "FFmpeg found via pkg-config")
        endif()
    endif()

    if(NOT FFMPEG_LIBS)
        # pkg-config fallback: search common library paths
        foreach(lib_name IN ITEMS avformat avcodec swresample avutil)
            find_library(FFMPEG_${lib_name}_LIB ${lib_name}
                PATHS
                    /opt/homebrew/lib
                    /usr/local/lib
                    /usr/lib
                    /opt/local/lib
            )
            if(NOT FFMPEG_${lib_name}_LIB)
                message(FATAL_ERROR "FFmpeg library '${lib_name}' not found. \
Please install FFmpeg development libraries (e.g., 'brew install ffmpeg' or \
'sudo apt install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev')")
            endif()
            list(APPEND FFMPEG_LIB_PATHS ${FFMPEG_${lib_name}_LIB})
        endforeach()

        # Find include directory
        find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
            PATHS
                /opt/homebrew/include
                /usr/local/include
                /usr/include
                /opt/local/include
        )
        if(NOT FFMPEG_INCLUDE_DIR)
            message(FATAL_ERROR "FFmpeg headers not found. Please install FFmpeg development libraries.")
        endif()

        add_library(ffmpeg_imported INTERFACE)
        target_link_libraries(ffmpeg_imported INTERFACE ${FFMPEG_LIB_PATHS})
        target_include_directories(ffmpeg_imported INTERFACE ${FFMPEG_INCLUDE_DIR})
        set(FFMPEG_LIBS ffmpeg_imported)
        message(STATUS "FFmpeg found via find_library fallback")
    endif()
else()
    # Android: link against prebuilt FFmpeg from ffmpeg-build/
    set(FFMPEG_ANDROID_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-build)
    set(FFMPEG_LIB_DIR ${FFMPEG_ANDROID_ROOT}/${ANDROID_ABI}/lib)
    set(FFMPEG_INCLUDE_DIR ${FFMPEG_ANDROID_ROOT}/${ANDROID_ABI}/include)

    if(NOT EXISTS ${FFMPEG_LIB_DIR})
        message(FATAL_ERROR "FFmpeg prebuilt libraries not found for ABI '${ANDROID_ABI}' at ${FFMPEG_LIB_DIR}. \
Run ffmpeg-android-maker first and copy output to ffmpeg-build/")
    endif()

    foreach(lib_name IN ITEMS avformat avcodec swresample avutil)
        add_library(ffmpeg_${lib_name} SHARED IMPORTED)
        set_target_properties(ffmpeg_${lib_name} PROPERTIES
            IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/lib${lib_name}.so
            INTERFACE_INCLUDE_DIRECTORIES ${FFMPEG_INCLUDE_DIR}
        )
    endforeach()

    set(FFMPEG_LIBS ffmpeg_avformat ffmpeg_avcodec ffmpeg_swresample ffmpeg_avutil)
    message(STATUS "Android build: using prebuilt FFmpeg from ${FFMPEG_LIB_DIR}")
endif()

# Core library
add_library(ffsubsync_core STATIC
    src/core/pipeline.cpp
    src/aligners/fft_aligner.cpp
    src/aligners/max_score_aligner.cpp
    src/aligners/golden_section_search.cpp
    src/speech/subtitle_speech.cpp
    src/speech/vad_processor.cpp
    src/media/ffmpeg_audio_decoder.cpp
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
        # spdlog::spdlog
        ${FFMPEG_LIBS}
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
        tests/test_ffmpeg_audio_decoder.cpp
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
