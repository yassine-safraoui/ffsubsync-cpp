#include "ffsubsync/ffmpeg_audio_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <android/log.h>

#define DECODER_LOG_TAG "FFmpegAudioDecoder"
#define DEC_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, DECODER_LOG_TAG, __VA_ARGS__)
#define DEC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, DECODER_LOG_TAG, __VA_ARGS__)

namespace ffsubsync {

// RAII deleters for FFmpeg objects
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) {
            swr_free(&ctx);
        }
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

struct AVIOContextDeleter {
    void operator()(AVIOContext* ctx) const {
        if (ctx) {
            // Do NOT close the underlying fd here — caller owns it.
            av_freep(&ctx->buffer);
            avio_context_free(&ctx);
        }
    }
};

using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVIOContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

class FFmpegAudioDecoder::Impl {
public:
    AVFormatContextPtr fmt_ctx;
    AVCodecContextPtr codec_ctx;
    SwrContextPtr swr_ctx;
    AVFramePtr frame;
    AVPacketPtr packet;
    AVIOContextPtr avio_ctx;
    int* fd_ptr = nullptr;  // Owned by Impl; allocated with new when using fd-based open.

    int stream_idx = -1;
    FFmpegAudioDecoder::Config config{};
    FFmpegAudioDecoder::StreamInfo stream_info{};
    std::string last_error;
    bool open_ = false;

    // Internal resampled buffer. Reused across next() calls.
    std::vector<float> resampled_buffer;

    // Pending resampled samples that haven't been returned yet.
    std::vector<float> pending_samples;
    size_t pending_offset = 0;

    bool init_resampler(const AVCodecContext* codec_ctx_raw) {
        swr_ctx.reset(swr_alloc());
        if (!swr_ctx) {
            last_error = "Failed to allocate SwrContext";
            return false;
        }

        int64_t in_layout = codec_ctx_raw->ch_layout.u.mask;
        int in_channels = codec_ctx_raw->ch_layout.nb_channels;
        AVSampleFormat in_fmt = codec_ctx_raw->sample_fmt;

        AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_MONO;
        int out_channels = 1;
        AVSampleFormat out_fmt = AV_SAMPLE_FMT_FLT;

        av_opt_set_chlayout(swr_ctx.get(), "in_chlayout", &codec_ctx_raw->ch_layout, 0);
        av_opt_set_int(swr_ctx.get(), "in_sample_rate", codec_ctx_raw->sample_rate, 0);
        av_opt_set_sample_fmt(swr_ctx.get(), "in_sample_fmt", in_fmt, 0);

        av_opt_set_chlayout(swr_ctx.get(), "out_chlayout", &out_layout, 0);
        av_opt_set_int(swr_ctx.get(), "out_sample_rate", config.target_sample_rate, 0);
        av_opt_set_sample_fmt(swr_ctx.get(), "out_sample_fmt", out_fmt, 0);

        int ret = swr_init(swr_ctx.get());
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            last_error = std::string("swr_init failed: ") + errbuf;
            swr_ctx.reset();
            return false;
        }

        return true;
    }

    bool read_and_decode_frame() {
        while (true) {
            int ret = av_read_frame(fmt_ctx.get(), packet.get());
            if (ret == AVERROR_EOF) {
                // Flush decoder.
                avcodec_send_packet(codec_ctx.get(), nullptr);
                return false;
            }
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                last_error = std::string("av_read_frame failed: ") + errbuf;
                return false;
            }

            if (packet->stream_index != stream_idx) {
                av_packet_unref(packet.get());
                continue;
            }

            ret = avcodec_send_packet(codec_ctx.get(), packet.get());
            av_packet_unref(packet.get());
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                last_error = std::string("avcodec_send_packet failed: ") + errbuf;
                return false;
            }

            ret = avcodec_receive_frame(codec_ctx.get(), frame.get());
            if (ret == AVERROR(EAGAIN)) {
                continue; // Need more packets.
            }
            if (ret == AVERROR_EOF) {
                return false;
            }
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                last_error = std::string("avcodec_receive_frame failed: ") + errbuf;
                return false;
            }

            // Got a frame.
            return true;
        }
    }

    bool resample_frame() {
        if (!frame->nb_samples) {
            return false;
        }

        // Compute output sample count.
        int out_samples = swr_get_out_samples(swr_ctx.get(), frame->nb_samples);
        if (out_samples <= 0) {
            return false;
        }

        resampled_buffer.resize(static_cast<size_t>(out_samples));

        const uint8_t** in_data = (const uint8_t**)frame->extended_data;
        uint8_t* out_data = reinterpret_cast<uint8_t*>(resampled_buffer.data());

        int converted = swr_convert(swr_ctx.get(), &out_data, out_samples,
                                   in_data, frame->nb_samples);
        if (converted < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(converted, errbuf, sizeof(errbuf));
            last_error = std::string("swr_convert failed: ") + errbuf;
            return false;
        }

        if (converted > 0) {
            resampled_buffer.resize(static_cast<size_t>(converted));
            pending_samples.insert(pending_samples.end(),
                                   resampled_buffer.begin(),
                                   resampled_buffer.end());
        }

        return true;
    }
};

// Static callback for AVIOContext read from fd.
static int read_packet_callback(void* opaque, uint8_t* buf, int buf_size) {
    int fd = *static_cast<int*>(opaque);
    int ret = static_cast<int>(::read(fd, buf, static_cast<size_t>(buf_size)));
    if (ret < 0) {
        return AVERROR(errno);
    }
    if (ret == 0) {
        return AVERROR_EOF;
    }
    return ret;
}

// Static callback for AVIOContext seek on fd.
static int64_t seek_callback(void* opaque, int64_t offset, int whence) {
    int fd = *static_cast<int*>(opaque);
    switch (whence) {
        case AVSEEK_SIZE:
            // Return file size without seeking, or -1 if unknown.
            // Some Android content:// fds don't support fstat meaningfully.
            {
                struct stat st;
                if (fstat(fd, &st) < 0 || st.st_size == 0) {
                    return -1;
                }
                return static_cast<int64_t>(st.st_size);
            }
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            {
                int64_t result = lseek64(fd, offset, whence);
                if (result < 0) {
                    DEC_LOGE("seek_callback: lseek64 failed (fd=%d, offset=%lld, whence=%d): %s",
                             fd, (long long)offset, whence, strerror(errno));
                    return AVERROR(errno);
                }
                return result;
            }
        default:
            return AVERROR(EINVAL);
    }
}

FFmpegAudioDecoder::FFmpegAudioDecoder() = default;
FFmpegAudioDecoder::~FFmpegAudioDecoder() = default;

FFmpegAudioDecoder::FFmpegAudioDecoder(FFmpegAudioDecoder&&) noexcept = default;
FFmpegAudioDecoder& FFmpegAudioDecoder::operator=(FFmpegAudioDecoder&&) noexcept = default;

bool FFmpegAudioDecoder::open(const std::filesystem::path& path, const Config& config) {
    close();
    impl_ = std::make_unique<Impl>();
    impl_->config = config;

    AVFormatContext* raw_fmt_ctx = nullptr;
    int ret = avformat_open_input(&raw_fmt_ctx, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avformat_open_input failed: ") + errbuf;
        return false;
    }
    impl_->fmt_ctx.reset(raw_fmt_ctx);

    ret = avformat_find_stream_info(impl_->fmt_ctx.get(), nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avformat_find_stream_info failed: ") + errbuf;
        return false;
    }

    // Find first audio stream.
    for (unsigned int i = 0; i < impl_->fmt_ctx->nb_streams; ++i) {
        if (impl_->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            impl_->stream_idx = static_cast<int>(i);
            break;
        }
    }

    if (impl_->stream_idx == -1) {
        impl_->last_error = "No audio stream found in file";
        return false;
    }

    AVStream* stream = impl_->fmt_ctx->streams[impl_->stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        impl_->last_error = "Codec not found";
        return false;
    }

    impl_->codec_ctx.reset(avcodec_alloc_context3(codec));
    if (!impl_->codec_ctx) {
        impl_->last_error = "Failed to allocate codec context";
        return false;
    }

    ret = avcodec_parameters_to_context(impl_->codec_ctx.get(), stream->codecpar);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avcodec_parameters_to_context failed: ") + errbuf;
        return false;
    }

    ret = avcodec_open2(impl_->codec_ctx.get(), codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avcodec_open2 failed: ") + errbuf;
        return false;
    }

    impl_->stream_info.duration_seconds =
        stream->duration > 0
            ? static_cast<double>(stream->duration) * av_q2d(stream->time_base)
            : static_cast<double>(impl_->fmt_ctx->duration) / AV_TIME_BASE;
    if (impl_->stream_info.duration_seconds < 0) {
        impl_->stream_info.duration_seconds = 0.0;
    }
    impl_->stream_info.original_sample_rate = impl_->codec_ctx->sample_rate;
    impl_->stream_info.original_channels = impl_->codec_ctx->ch_layout.nb_channels;
    impl_->stream_info.codec_name = codec->name ? codec->name : "unknown";

    if (!impl_->init_resampler(impl_->codec_ctx.get())) {
        return false;
    }

    impl_->frame.reset(av_frame_alloc());
    impl_->packet.reset(av_packet_alloc());

    // Seek to start_seconds if > 0.
    if (config.start_seconds > 0.0) {
        int64_t ts = static_cast<int64_t>(config.start_seconds / av_q2d(stream->time_base));
        av_seek_frame(impl_->fmt_ctx.get(), impl_->stream_idx, ts, AVSEEK_FLAG_BACKWARD);
    }

    impl_->open_ = true;
    return true;
}

bool FFmpegAudioDecoder::open(int fd, const Config& config) {
    close();
    impl_ = std::make_unique<Impl>();
    impl_->config = config;

    // Seek fd to beginning so FFmpeg can parse headers correctly.
    off_t seek_result = lseek(fd, 0, SEEK_SET);
    if (seek_result < 0) {
        int err = errno;
        DEC_LOGE("lseek(fd=%d, 0, SEEK_SET) failed: %s", fd, strerror(err));
        impl_->last_error = std::string("lseek failed: ") + strerror(err);
        return false;
    }
    DEC_LOGD("Opened fd=%d, seeked to position %lld", fd, (long long)seek_result);

    // Log file size for debugging.
    struct stat st;
    if (fstat(fd, &st) == 0) {
        DEC_LOGD("fd=%d file size: %lld bytes", fd, (long long)st.st_size);
    } else {
        DEC_LOGE("fd=%d fstat failed: %s", fd, strerror(errno));
    }

    // Allocate buffer for AVIOContext.
    constexpr int kAvioBufferSize = 32768;
    unsigned char* avio_buffer = static_cast<unsigned char*>(av_malloc(kAvioBufferSize));
    if (!avio_buffer) {
        impl_->last_error = "Failed to allocate AVIO buffer";
        return false;
    }

    // We store the fd in the Impl so the callback can read it.
    // Use a small helper: store fd in a heap int that we manage.
    impl_->fd_ptr = new int(fd);
    int* fd_ptr = impl_->fd_ptr;
    impl_->avio_ctx.reset(avio_alloc_context(
        avio_buffer, kAvioBufferSize, 0, fd_ptr, read_packet_callback, nullptr, seek_callback));
    if (!impl_->avio_ctx) {
        delete impl_->fd_ptr;
        impl_->fd_ptr = nullptr;
        av_freep(&avio_buffer);
        impl_->last_error = "Failed to allocate AVIOContext";
        return false;
    }

    AVFormatContext* raw_fmt_ctx = avformat_alloc_context();
    if (!raw_fmt_ctx) {
        impl_->last_error = "Failed to allocate AVFormatContext";
        return false;
    }
    raw_fmt_ctx->pb = impl_->avio_ctx.get();

    int ret = avformat_open_input(&raw_fmt_ctx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        DEC_LOGE("avformat_open_input (fd=%d) failed: ret=%d (%s)", fd, ret, errbuf);
        impl_->last_error = std::string("avformat_open_input (fd) failed: ") + errbuf;
        return false;
    }
    DEC_LOGD("avformat_open_input succeeded, format: %s", raw_fmt_ctx->iformat ? raw_fmt_ctx->iformat->name : "unknown");
    impl_->fmt_ctx.reset(raw_fmt_ctx);

    ret = avformat_find_stream_info(impl_->fmt_ctx.get(), nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avformat_find_stream_info (fd) failed: ") + errbuf;
        return false;
    }

    // Find first audio stream.
    for (unsigned int i = 0; i < impl_->fmt_ctx->nb_streams; ++i) {
        if (impl_->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            impl_->stream_idx = static_cast<int>(i);
            break;
        }
    }

    if (impl_->stream_idx == -1) {
        impl_->last_error = "No audio stream found in file descriptor";
        return false;
    }

    AVStream* stream = impl_->fmt_ctx->streams[impl_->stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        impl_->last_error = "Codec not found";
        return false;
    }

    impl_->codec_ctx.reset(avcodec_alloc_context3(codec));
    if (!impl_->codec_ctx) {
        impl_->last_error = "Failed to allocate codec context";
        return false;
    }

    ret = avcodec_parameters_to_context(impl_->codec_ctx.get(), stream->codecpar);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avcodec_parameters_to_context failed: ") + errbuf;
        return false;
    }

    ret = avcodec_open2(impl_->codec_ctx.get(), codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        impl_->last_error = std::string("avcodec_open2 failed: ") + errbuf;
        return false;
    }

    impl_->stream_info.duration_seconds =
        stream->duration > 0
            ? static_cast<double>(stream->duration) * av_q2d(stream->time_base)
            : static_cast<double>(impl_->fmt_ctx->duration) / AV_TIME_BASE;
    if (impl_->stream_info.duration_seconds < 0) {
        impl_->stream_info.duration_seconds = 0.0;
    }
    impl_->stream_info.original_sample_rate = impl_->codec_ctx->sample_rate;
    impl_->stream_info.original_channels = impl_->codec_ctx->ch_layout.nb_channels;
    impl_->stream_info.codec_name = codec->name ? codec->name : "unknown";

    if (!impl_->init_resampler(impl_->codec_ctx.get())) {
        return false;
    }

    impl_->frame.reset(av_frame_alloc());
    impl_->packet.reset(av_packet_alloc());

    if (config.start_seconds > 0.0) {
        int64_t ts = static_cast<int64_t>(config.start_seconds / av_q2d(stream->time_base));
        av_seek_frame(impl_->fmt_ctx.get(), impl_->stream_idx, ts, AVSEEK_FLAG_BACKWARD);
    }

    impl_->open_ = true;
    return true;
}

void FFmpegAudioDecoder::close() {
    if (impl_) {
        impl_->fmt_ctx.reset();
        impl_->codec_ctx.reset();
        impl_->swr_ctx.reset();
        impl_->frame.reset();
        impl_->packet.reset();
        impl_->avio_ctx.reset();
        delete impl_->fd_ptr;
        impl_->fd_ptr = nullptr;
        impl_->open_ = false;
        impl_->pending_samples.clear();
        impl_->pending_offset = 0;
    }
}

bool FFmpegAudioDecoder::next(AudioChunk& chunk) {
    if (!impl_ || !impl_->open_) {
        return false;
    }

    // If we have pending samples from a previous frame, return them.
    if (impl_->pending_offset < impl_->pending_samples.size()) {
        chunk.data = impl_->pending_samples.data() + impl_->pending_offset;
        chunk.num_samples = static_cast<int>(impl_->pending_samples.size() - impl_->pending_offset);
        impl_->pending_offset = impl_->pending_samples.size();
        return true;
    }

    // Decode next frame.
    if (!impl_->read_and_decode_frame()) {
        // Flush any remaining resampler data.
        if (impl_->swr_ctx) {
            int out_samples = swr_get_out_samples(impl_->swr_ctx.get(), 0);
            if (out_samples > 0) {
                impl_->resampled_buffer.resize(static_cast<size_t>(out_samples));
                uint8_t* out_data = reinterpret_cast<uint8_t*>(impl_->resampled_buffer.data());
                int converted = swr_convert(impl_->swr_ctx.get(), &out_data, out_samples,
                                            nullptr, 0);
                if (converted > 0) {
                    impl_->resampled_buffer.resize(static_cast<size_t>(converted));
                    chunk.data = impl_->resampled_buffer.data();
                    chunk.num_samples = converted;
                    return true;
                }
            }
        }
        return false;
    }

    // Resample the decoded frame.
    if (!impl_->resample_frame()) {
        return false;
    }

    // Return the newly resampled data.
    if (impl_->pending_offset < impl_->pending_samples.size()) {
        chunk.data = impl_->pending_samples.data() + impl_->pending_offset;
        chunk.num_samples = static_cast<int>(impl_->pending_samples.size() - impl_->pending_offset);
        impl_->pending_offset = impl_->pending_samples.size();
        return true;
    }

    return false;
}

const FFmpegAudioDecoder::StreamInfo& FFmpegAudioDecoder::info() const {
    static const FFmpegAudioDecoder::StreamInfo empty{};
    if (!impl_) return empty;
    return impl_->stream_info;
}

bool FFmpegAudioDecoder::is_open() const {
    return impl_ && impl_->open_;
}

std::string FFmpegAudioDecoder::error_message() const {
    if (!impl_) return "";
    return impl_->last_error;
}

} // namespace ffsubsync
