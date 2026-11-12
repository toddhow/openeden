#include "audio_pcm.hpp"

// Voice audio through FFmpeg (the game's own dependency): Morrowind's mp3, the xWMA in Skyrim's .fuz, ogg, ...
// The data is read from memory through a custom AVIOContext. Everything is released by the guards below on every path.

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <memory>

namespace face_editor
{
    namespace
    {
        struct MemoryReader
        {
            const std::uint8_t* mData = nullptr;
            std::size_t mSize = 0;
            std::size_t mPos = 0;
        };

        int readPacket(void* opaque, std::uint8_t* buffer, int size)
        {
            auto* reader = static_cast<MemoryReader*>(opaque);
            if (reader->mPos >= reader->mSize)
                return AVERROR_EOF;
            const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(size), reader->mSize - reader->mPos);
            std::memcpy(buffer, reader->mData + reader->mPos, n);
            reader->mPos += n;
            return static_cast<int>(n);
        }

        std::int64_t seekPacket(void* opaque, std::int64_t offset, int whence)
        {
            auto* reader = static_cast<MemoryReader*>(opaque);
            if (whence & AVSEEK_SIZE)
                return static_cast<std::int64_t>(reader->mSize);
            whence &= ~AVSEEK_FORCE;
            std::int64_t target = 0;
            if (whence == SEEK_SET)
                target = offset;
            else if (whence == SEEK_CUR)
                target = static_cast<std::int64_t>(reader->mPos) + offset;
            else if (whence == SEEK_END)
                target = static_cast<std::int64_t>(reader->mSize) + offset;
            else
                return -1;
            if (target < 0 || target > static_cast<std::int64_t>(reader->mSize))
                return -1;
            reader->mPos = static_cast<std::size_t>(target);
            return target;
        }

        std::string avText(int code)
        {
            char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(code, buffer, sizeof(buffer));
            return buffer;
        }

        struct FormatGuard
        {
            AVFormatContext* mContext = nullptr;
            AVIOContext* mIo = nullptr;
            ~FormatGuard()
            {
                if (mContext != nullptr)
                    avformat_close_input(&mContext);
                if (mIo != nullptr)
                {
                    av_freep(&mIo->buffer);
                    avio_context_free(&mIo);
                }
            }
        };

        struct CodecGuard
        {
            AVCodecContext* mContext = nullptr;
            ~CodecGuard() { avcodec_free_context(&mContext); }
        };

        struct PacketGuard
        {
            AVPacket* mPacket = av_packet_alloc();
            ~PacketGuard() { av_packet_free(&mPacket); }
        };

        struct FrameGuard
        {
            AVFrame* mFrame = av_frame_alloc();
            ~FrameGuard() { av_frame_free(&mFrame); }
        };

        struct ResampleGuard
        {
            SwrContext* mContext = nullptr;
            ~ResampleGuard() { swr_free(&mContext); }
        };

        /// Converts one frame (or, with a null frame, flushes the resampler) and appends mono floats. False with
        /// \a error set on failure or when the cap is passed.
        bool appendFrame(ResampleGuard& resampler, const AVFrame* frame, Pcm& out, std::string& error)
        {
            if (frame != nullptr && resampler.mContext == nullptr)
            {
                AVChannelLayout mono;
                av_channel_layout_default(&mono, 1);
                AVChannelLayout input;
                if (av_channel_layout_copy(&input, &frame->ch_layout) < 0 || input.nb_channels == 0)
                    av_channel_layout_default(&input, std::max(1, frame->ch_layout.nb_channels));
                const int rc = swr_alloc_set_opts2(&resampler.mContext, &mono, AV_SAMPLE_FMT_FLT, frame->sample_rate,
                    &input, static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&input);
                if (rc < 0 || swr_init(resampler.mContext) < 0)
                {
                    error = "cannot set up the sample conversion";
                    return false;
                }
                out.mRate = static_cast<std::uint32_t>(frame->sample_rate);
            }
            if (resampler.mContext == nullptr)
                return true;
            const int inSamples = frame != nullptr ? frame->nb_samples : 0;
            const int capacity = std::max(1, swr_get_out_samples(resampler.mContext, inSamples)) + 256;
            std::vector<float> buffer(static_cast<std::size_t>(capacity));
            std::uint8_t* planes[1] = { reinterpret_cast<std::uint8_t*>(buffer.data()) };
            const int got = swr_convert(resampler.mContext, planes, capacity,
                frame != nullptr ? const_cast<const std::uint8_t**>(frame->extended_data) : nullptr, inSamples);
            if (got < 0)
            {
                error = "sample conversion failed: " + avText(got);
                return false;
            }
            if (out.mSamples.size() + static_cast<std::size_t>(got) > AudioLimits::sMaxSamples)
            {
                error = "the audio is longer than the " + std::to_string(AudioLimits::sMaxSamples)
                    + " samples this editor keeps";
                return false;
            }
            for (int i = 0; i < got; ++i)
                out.mSamples.push_back(std::isfinite(buffer[i]) ? std::clamp(buffer[i], -1.f, 1.f) : 0.f);
            return true;
        }
    }

    bool decodeWithFfmpeg(std::string_view bytes, Pcm& out, std::string& error)
    {
        out = Pcm();
        error.clear();
        if (bytes.empty() || bytes.size() > AudioLimits::sMaxFileBytes)
        {
            error = bytes.empty() ? "no audio data" : "the audio data is too large";
            return false;
        }
        MemoryReader reader{ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), 0 };
        FormatGuard format;
        constexpr int kIoBuffer = 32 * 1024;
        auto* ioBuffer = static_cast<std::uint8_t*>(av_malloc(kIoBuffer));
        if (ioBuffer == nullptr)
        {
            error = "out of memory";
            return false;
        }
        format.mIo = avio_alloc_context(ioBuffer, kIoBuffer, 0, &reader, &readPacket, nullptr, &seekPacket);
        if (format.mIo == nullptr)
        {
            av_free(ioBuffer);
            error = "out of memory";
            return false;
        }
        format.mContext = avformat_alloc_context();
        if (format.mContext == nullptr)
        {
            error = "out of memory";
            return false;
        }
        format.mContext->pb = format.mIo;
        int rc = avformat_open_input(&format.mContext, nullptr, nullptr, nullptr);
        if (rc < 0)
        {
            error = "FFmpeg cannot open the data as audio: " + avText(rc);
            return false;
        }
        rc = avformat_find_stream_info(format.mContext, nullptr);
        if (rc < 0)
        {
            error = "FFmpeg found no stream information: " + avText(rc);
            return false;
        }
        const AVCodec* codec = nullptr;
        const int stream = av_find_best_stream(format.mContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream < 0 || codec == nullptr)
        {
            error = "the data has no audio stream FFmpeg can decode";
            return false;
        }
        CodecGuard decoder;
        decoder.mContext = avcodec_alloc_context3(codec);
        if (decoder.mContext == nullptr
            || avcodec_parameters_to_context(decoder.mContext, format.mContext->streams[stream]->codecpar) < 0)
        {
            error = "cannot set up the decoder";
            return false;
        }
        rc = avcodec_open2(decoder.mContext, codec, nullptr);
        if (rc < 0)
        {
            error = "cannot open the decoder: " + avText(rc);
            return false;
        }

        PacketGuard packet;
        FrameGuard frame;
        ResampleGuard resampler;
        if (packet.mPacket == nullptr || frame.mFrame == nullptr)
        {
            error = "out of memory";
            return false;
        }
        auto drain = [&]() -> int {
            // 0: need more input, 1: end of stream, negative: failure
            for (;;)
            {
                const int r = avcodec_receive_frame(decoder.mContext, frame.mFrame);
                if (r == AVERROR(EAGAIN))
                    return 0;
                if (r == AVERROR_EOF)
                    return 1;
                if (r < 0)
                {
                    error = "decoding failed: " + avText(r);
                    return -1;
                }
                const bool ok = appendFrame(resampler, frame.mFrame, out, error);
                av_frame_unref(frame.mFrame);
                if (!ok)
                    return -1;
            }
        };
        bool failed = false;
        while (!failed && av_read_frame(format.mContext, packet.mPacket) >= 0)
        {
            if (packet.mPacket->stream_index == stream)
            {
                if (avcodec_send_packet(decoder.mContext, packet.mPacket) >= 0)
                    failed = drain() < 0;
                // A packet the decoder refuses is skipped, as players do; a file full of them ends with no samples.
            }
            av_packet_unref(packet.mPacket);
        }
        if (!failed)
        {
            avcodec_send_packet(decoder.mContext, nullptr);
            failed = drain() < 0;
        }
        if (!failed)
            failed = !appendFrame(resampler, nullptr, out, error);
        if (failed)
        {
            out = Pcm();
            return false;
        }
        if (out.mSamples.empty() || out.mRate == 0)
        {
            out = Pcm();
            error = "FFmpeg decoded no samples";
            return false;
        }
        return true;
    }
}
