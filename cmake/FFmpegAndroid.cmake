include(ExternalProject)

set(FFMPEG_PREBUILT_DIR "" CACHE PATH
    "Path to prebuilt FFmpeg directory (skips auto-build)")
set(FFMPEG_GIT_TAG "n8.1" CACHE STRING
    "FFmpeg git tag or branch to build")

function(setup_ffmpeg_android)
    if(NOT ANDROID)
        message(FATAL_ERROR "setup_ffmpeg_android() called on non-Android build")
    endif()

    # ── 2a — Early return for prebuilt ─────────────────────────────────────────
    if(FFMPEG_PREBUILT_DIR)
        set(_prebuilt_lib ${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/lib/libavformat.so)
        if(NOT EXISTS ${_prebuilt_lib})
            message(FATAL_ERROR "FFMPEG_PREBUILT_DIR set but libavformat.so not found at ${_prebuilt_lib}")
        endif()

        set(_prebuilt_inc ${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/include)
        foreach(lib_name IN ITEMS avformat avcodec swresample avutil)
            add_library(ffmpeg_${lib_name} SHARED IMPORTED GLOBAL)
            set_target_properties(ffmpeg_${lib_name} PROPERTIES
                IMPORTED_LOCATION ${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}/lib/lib${lib_name}.so
                INTERFACE_INCLUDE_DIRECTORIES ${_prebuilt_inc}
            )
        endforeach()
        set_target_properties(ffmpeg_avformat PROPERTIES
            INTERFACE_LINK_LIBRARIES "ffmpeg_avcodec;ffmpeg_swresample;ffmpeg_avutil"
        )

        set(FFMPEG_LIBS
            ffmpeg_avformat ffmpeg_avcodec ffmpeg_swresample ffmpeg_avutil
            PARENT_SCOPE)
        message(STATUS "FFmpeg Android: using prebuilt from ${FFMPEG_PREBUILT_DIR}/${ANDROID_ABI}")
        return()
    endif()

    # ── 2b — Output paths ─────────────────────────────────────────────────────
    set(FFMPEG_INSTALL_DIR ${CMAKE_BINARY_DIR}/ffmpeg-android/${ANDROID_ABI} PARENT_SCOPE)
    set(FFMPEG_INSTALL_DIR ${CMAKE_BINARY_DIR}/ffmpeg-android/${ANDROID_ABI})
    set(FFMPEG_LIB_DIR     ${FFMPEG_INSTALL_DIR}/lib)
    set(FFMPEG_INCLUDE_DIR ${FFMPEG_INSTALL_DIR}/include)

    # ── 2c — NDK toolchain command strings ────────────────────────────────────
    if(NOT DEFINED ANDROID_NDK_HOME)
        if(DEFINED ANDROID_NDK)
            set(ANDROID_NDK_HOME ${ANDROID_NDK})
        elseif(DEFINED ENV{ANDROID_NDK_HOME})
            set(ANDROID_NDK_HOME $ENV{ANDROID_NDK_HOME})
        else()
            message(FATAL_ERROR "ANDROID_NDK_HOME is not set")
        endif()
    endif()

    # Detect host tag
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(HOST_TAG "darwin-x86_64")
    else()
        set(HOST_TAG "linux-x86_64")
    endif()

    set(TOOLCHAIN_PATH ${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${HOST_TAG})

    # Determine API level (minimum 21 for 64-bit)
    if(DEFINED ANDROID_PLATFORM)
        string(REGEX REPLACE "android-" "" _api "${ANDROID_PLATFORM}")
    elseif(DEFINED ANDROID_NATIVE_API_LEVEL)
        set(_api ${ANDROID_NATIVE_API_LEVEL})
    else()
        set(_api 21)
    endif()

    # Map ABI to arch and target triple
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(ARCH "aarch64")
        set(TARGET_TRIPLE "aarch64-linux-android")
        if(_api LESS 21)
            set(_api 21)
        endif()
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(ARCH "arm")
        set(TARGET_TRIPLE "armv7a-linux-androideabi")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(ARCH "i686")
        set(TARGET_TRIPLE "i686-linux-android")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(ARCH "x86_64")
        set(TARGET_TRIPLE "x86_64-linux-android")
        if(_api LESS 21)
            set(_api 21)
        endif()
    else()
        message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI}")
    endif()

    set(FAM_CC    ${TOOLCHAIN_PATH}/bin/${TARGET_TRIPLE}${_api}-clang)
    set(FAM_AR    ${TOOLCHAIN_PATH}/bin/llvm-ar)
    set(FAM_STRIP ${TOOLCHAIN_PATH}/bin/llvm-strip)
    set(FAM_RANLIB ${TOOLCHAIN_PATH}/bin/llvm-ranlib)
    set(FAM_NM    ${TOOLCHAIN_PATH}/bin/llvm-nm)
    set(SYSROOT   ${TOOLCHAIN_PATH}/sysroot)

    # ── 2d — FFmpeg configure flags ───────────────────────────────────────────
    set(FFMPEG_COMMON_FLAGS
        --prefix=${FFMPEG_INSTALL_DIR}
        --enable-cross-compile
        --target-os=android
        --arch=${ARCH}
        --sysroot=${SYSROOT}
        --cc=${FAM_CC}
        --cxx=${FAM_CC}++
        --ld=${FAM_CC}
        --ar=${FAM_AR}
        --as=${FAM_CC}
        --nm=${FAM_NM}
        --ranlib=${FAM_RANLIB}
        --strip=${FAM_STRIP}

        --enable-shared
        --disable-static
        --disable-vulkan
        --disable-everything
        --disable-programs
        --disable-doc
        --disable-network
        --disable-zlib
        --disable-bzlib
        --disable-iconv
        --enable-decoder=aac,mp3,ac3,eac3,flac,pcm_s16le,pcm_s24le,vorbis,opus,dca
        --enable-encoder=pcm_s16le
        --enable-demuxer=mov,matroska,avi,mp3,wav,flac,ogg
        --enable-muxer=wav
        --enable-parser=aac,ac3,eac3,flac,mpegaudio,vorbis,opus,dca
        --enable-protocol=file
    )

    # ABI-specific flags
    if(ANDROID_ABI STREQUAL "x86")
        list(APPEND FFMPEG_COMMON_FLAGS --disable-asm)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        list(APPEND FFMPEG_COMMON_FLAGS --x86asmexe=nasm)
    endif()

    # Detect parallel jobs
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC EQUAL 0)
        set(NPROC 2)
    endif()

    set(FFMPEG_SOURCE_DIR ${CMAKE_BINARY_DIR}/ffmpeg-android-src)

    # Write a configure wrapper script to avoid cmake -E env quoting issues
    # with flags that contain spaces (e.g. CFLAGS="-O3 -fPIC").
    set(FFMPEG_CONFIGURE_SCRIPT ${CMAKE_BINARY_DIR}/ffmpeg-android-prefix/${ANDROID_ABI}/configure_ffmpeg.sh)
    string(REPLACE ";" " " FFMPEG_FLAGS_STR "${FFMPEG_COMMON_FLAGS}")
    file(WRITE ${FFMPEG_CONFIGURE_SCRIPT} "#!/bin/bash
set -e
export PATH=\"${TOOLCHAIN_PATH}/bin:\$PATH\"
export CFLAGS=\"-O3 -fPIC\"
export LDFLAGS=\"-Wl,-z,max-page-size=16384\"
exec \"${FFMPEG_SOURCE_DIR}/configure\" ${FFMPEG_FLAGS_STR}
")

    # ── 2e — ExternalProject_Add ──────────────────────────────────────────────
    ExternalProject_Add(ffmpeg_android_${ANDROID_ABI}
        GIT_REPOSITORY https://git.ffmpeg.org/ffmpeg.git
        GIT_TAG        ${FFMPEG_GIT_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE

        SOURCE_DIR  ${FFMPEG_SOURCE_DIR}
        PREFIX      ${CMAKE_BINARY_DIR}/ffmpeg-android-prefix/${ANDROID_ABI}

        CONFIGURE_COMMAND
            bash ${FFMPEG_CONFIGURE_SCRIPT}

        BUILD_COMMAND
            make -j${NPROC}

        INSTALL_COMMAND
            make install

        BUILD_BYPRODUCTS
            ${FFMPEG_LIB_DIR}/libavformat.so
            ${FFMPEG_LIB_DIR}/libavcodec.so
            ${FFMPEG_LIB_DIR}/libswresample.so
            ${FFMPEG_LIB_DIR}/libavutil.so
    )

    # ── 2f — Create imported targets ──────────────────────────────────────────
    # Pre-create include dir so CMake doesn't complain at configure time
    file(MAKE_DIRECTORY ${FFMPEG_INCLUDE_DIR})

    foreach(lib_name IN ITEMS avformat avcodec swresample avutil)
        add_library(ffmpeg_${lib_name} SHARED IMPORTED GLOBAL)
        set_target_properties(ffmpeg_${lib_name} PROPERTIES
            IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/lib${lib_name}.so
            INTERFACE_INCLUDE_DIRECTORIES ${FFMPEG_INCLUDE_DIR}
        )
        add_dependencies(ffmpeg_${lib_name} ffmpeg_android_${ANDROID_ABI})
    endforeach()

    set_target_properties(ffmpeg_avformat PROPERTIES
        INTERFACE_LINK_LIBRARIES "ffmpeg_avcodec;ffmpeg_swresample;ffmpeg_avutil"
    )

    # ── 2g — Export to parent scope ───────────────────────────────────────────
    set(FFMPEG_LIBS
        ffmpeg_avformat ffmpeg_avcodec ffmpeg_swresample ffmpeg_avutil
        PARENT_SCOPE)
endfunction()
