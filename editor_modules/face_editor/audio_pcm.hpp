#pragma once

// Voice audio for the face editor (task 0147): a file to mono float PCM. WAV is read here (PCM 8/16/24/32 bit and 32
// bit float, any channel count mixed down); Morrowind's .mp3 and the xWMA inside a Skyrim .fuz go through FFmpeg
// (audio_ffmpeg.cpp), which the game already links. Untrusted input: every size is checked against the bytes present
// and against caps before anything is allocated; nothing throws.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace face_editor
{
    struct Pcm
    {
        std::vector<float> mSamples; // mono, -1..1
        std::uint32_t mRate = 0;

        double durationSeconds() const { return mRate != 0 ? static_cast<double>(mSamples.size()) / mRate : 0.0; }
    };

    namespace AudioLimits
    {
        inline constexpr std::uint64_t sMaxFileBytes = 256ull * 1024 * 1024;
        inline constexpr std::uint64_t sMaxSamples = 16ull * 1024 * 1024; // mono samples kept: 6 minutes at 44.1 kHz
        inline constexpr std::uint32_t sMaxRate = 192000;
        inline constexpr std::uint32_t sMaxChannels = 8;
    }

    /// RIFF/WAVE. False with \a error ("what (offset N)") otherwise; \a out is then empty.
    bool decodeWav(std::string_view bytes, Pcm& out, std::string& error);

    /// Any other container FFmpeg reads (mp3, ogg, xWMA, ...), mixed to mono at the file's own rate. Implemented in
    /// audio_ffmpeg.cpp; false with \a error when FFmpeg is not available or the data is not decodable.
    bool decodeWithFfmpeg(std::string_view bytes, Pcm& out, std::string& error);

    /// By file extension and magic: .wav (or RIFF/WAVE) here, .fuz through components/fuz then FFmpeg for the xWMA, the
    /// rest through FFmpeg.
    bool loadVoiceFile(const std::filesystem::path& path, Pcm& out, std::string& error);

    /// The same from bytes already read; \a name is only used for the extension and error text.
    bool decodeVoiceBytes(std::string_view bytes, std::string_view name, Pcm& out, std::string& error);

    /// A little WAV (16 bit mono) for tests and for the fixture tool.
    std::string encodeWav16(const std::vector<float>& samples, std::uint32_t rate);
}
