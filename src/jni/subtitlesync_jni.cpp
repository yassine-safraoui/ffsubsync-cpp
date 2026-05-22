#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <vector>
#include <string>

#include "ffsubsync/ffmpeg_audio_decoder.h"
#include "ffsubsync/vad_processor.h"
#include "ffsubsync/srt_parser.h"
#include "ffsubsync/subtitle_speech.h"
#include "ffsubsync/aligner.h"

#define LOG_TAG "SubtitleSyncJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void throwJavaException(JNIEnv* env, const char* clazz, const char* msg) {
    jclass exClass = env->FindClass(clazz);
    if (exClass != nullptr) {
        env->ThrowNew(exClass, msg);
        env->DeleteLocalRef(exClass);
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_yassine_subtitlesync_SubtitleSync_nativeSyncSubtitles(
    JNIEnv* env,
    jclass /* clazz */,
    jint videoFd,
    jintArray subtitleFds,
    jstring modelPath) {

    if (modelPath == nullptr) {
        throwJavaException(env, "java/lang/IllegalArgumentException", "modelPath is null");
        close(static_cast<int>(videoFd));
        return nullptr;
    }

    const char* modelPathCStr = env->GetStringUTFChars(modelPath, nullptr);
    if (modelPathCStr == nullptr) {
        close(static_cast<int>(videoFd));
        return nullptr; // OutOfMemoryError already thrown
    }
    std::string model_path(modelPathCStr);
    env->ReleaseStringUTFChars(modelPath, modelPathCStr);

    jclass syncResultClass = env->FindClass("com/yassine/subtitlesync/SyncResult");
    if (syncResultClass == nullptr) {
        close(static_cast<int>(videoFd));
        return nullptr; // NoClassDefFoundError already thrown
    }
    jmethodID syncResultCtor = env->GetMethodID(syncResultClass, "<init>", "(DD)V");
    if (syncResultCtor == nullptr) {
        env->DeleteLocalRef(syncResultClass);
        close(static_cast<int>(videoFd));
        return nullptr;
    }

    // Read subtitle fd count and copy fds so we can close them all later.
    jsize numSubs = env->GetArrayLength(subtitleFds);
    jint* fds = env->GetIntArrayElements(subtitleFds, nullptr);
    if (fds == nullptr) {
        env->DeleteLocalRef(syncResultClass);
        close(static_cast<int>(videoFd));
        return nullptr;
    }

    // Copy fds to a local vector so we can release the JNI array early.
    std::vector<int> subFds(fds, fds + numSubs);
    env->ReleaseIntArrayElements(subtitleFds, fds, JNI_ABORT);

    // RAII-style fd cleanup: close video fd and all subtitle fds on every exit path.
    // This struct ensures fds are closed even on exceptions.
    struct FdGuard {
        int videoFd;
        std::vector<int> subFds;
        bool closed = false;
        ~FdGuard() {
            if (!closed) close_all();
        }
        void close_all() {
            close(videoFd);
            for (int fd : subFds) {
                close(fd);
            }
            closed = true;
        }
    } fdGuard{static_cast<int>(videoFd), subFds};

    try {
        // Open video decoder
        ffsubsync::FFmpegAudioDecoder decoder;
        ffsubsync::FFmpegAudioDecoder::Config config;
        config.target_sample_rate = 16000;
        config.target_channels = 1;

        if (!decoder.open(static_cast<int>(videoFd), config)) {
            LOGE("Failed to open video fd: %s", decoder.error_message().c_str());
            jobjectArray emptyArray = env->NewObjectArray(0, syncResultClass, nullptr);
            env->DeleteLocalRef(syncResultClass);
            fdGuard.close_all();
            return emptyArray;
        }

        // Stream audio through VAD
        ffsubsync::VADProcessor vad(model_path, 16000);
        ffsubsync::FFmpegAudioDecoder::AudioChunk chunk{};

        while (decoder.next(chunk)) {
            vad.feed(chunk.data, static_cast<size_t>(chunk.num_samples));
        }

        vad.flush();
        auto segments = vad.drain_segments();
        auto audio_speech = ffsubsync::VADProcessor::to_binary_vector(
            segments, 16000, 100.0f, decoder.info().duration_seconds);

        // Process subtitle files
        std::vector<ffsubsync::FFTAligner::Result> results;
        ffsubsync::FFTAligner aligner;

        for (jsize i = 0; i < numSubs; ++i) {
            int fd = subFds[i];

            // Seek to beginning before reading.
            lseek(fd, 0, SEEK_SET);

            // Read entire fd content
            std::string content;
            char buffer[4096];
            ssize_t n;
            while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
                content.append(buffer, static_cast<size_t>(n));
            }

            auto subtitles = ffsubsync::SRTParser().parse_string(content);
            auto subtitle_speech = ffsubsync::extract_speech(subtitles, 100);
            auto alignment = aligner.align(audio_speech, subtitle_speech);
            results.push_back(alignment);
        }

        // Construct return array
        jobjectArray resultArray = env->NewObjectArray(
            static_cast<jsize>(results.size()), syncResultClass, nullptr);
        if (resultArray == nullptr) {
            env->DeleteLocalRef(syncResultClass);
            fdGuard.close_all();
            return nullptr;
        }

        for (size_t i = 0; i < results.size(); ++i) {
            double offsetSeconds = results[i].offset_samples / 100.0;
            jobject obj = env->NewObject(
                syncResultClass, syncResultCtor,
                static_cast<jdouble>(results[i].score),
                static_cast<jdouble>(offsetSeconds));
            if (obj == nullptr) {
                env->DeleteLocalRef(resultArray);
                env->DeleteLocalRef(syncResultClass);
                fdGuard.close_all();
                return nullptr;
            }
            env->SetObjectArrayElement(resultArray, static_cast<jsize>(i), obj);
            env->DeleteLocalRef(obj);
        }

        env->DeleteLocalRef(syncResultClass);
        fdGuard.close_all();
        return resultArray;

    } catch (const std::exception& e) {
        LOGE("Exception in nativeSyncSubtitles: %s", e.what());
        throwJavaException(env, "java/lang/RuntimeException", e.what());
        env->DeleteLocalRef(syncResultClass);
        fdGuard.close_all();
        return nullptr;
    }
}