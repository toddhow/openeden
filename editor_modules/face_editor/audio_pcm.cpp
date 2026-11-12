#include "audio_pcm.hpp"

#include <components/fuz/fuz.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace face_editor
{
    namespace
    {
        std::uint32_t le32(const unsigned char* p)
        {
            return std::uint32_t{ p[0] } | (std::uint32_t{ p[1] } << 8) | (std::uint32_t{ p[2] } << 16)
                | (std::uint32_t{ p[3] } << 24);
        }
        std::uint16_t le16(const unsigned char* p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        }

        std::string where(const std::string& what, std::size_t offset)
        {
            return what + " (offset " + std::to_string(offset) + ")";
        }

        bool isRiffWave(std::string_view bytes)
        {
            return bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WAVE") == 0;
        }
    }

    bool decodeWav(std::string_view bytes, Pcm& out, std::string& error)
    {
        out = Pcm();
        if (!isRiffWave(bytes))
        {
            error = where("not a RIFF/WAVE file", 0);
            return false;
        }
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
        std::size_t pos = 12;
        bool haveFmt = false;
        std::uint16_t format = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0;
        while (pos + 8 <= bytes.size())
        {
            const std::string_view id = bytes.substr(pos, 4);
            const std::uint64_t size = le32(data + pos + 4);
            const std::size_t body = pos + 8;
            const std::uint64_t available = bytes.size() - body;
            if (id == "fmt ")
            {
                if (size < 16 || available < 16)
                {
                    error = where("the fmt chunk is too short", pos);
                    return false;
                }
                format = le16(data + body);
                channels = le16(data + body + 2);
                rate = le32(data + body + 4);
                bits = le16(data + body + 14);
                if (format == 0xFFFE && size >= 26 && available >= 26)
                    format = le16(data + body + 24); // WAVE_FORMAT_EXTENSIBLE: the sub format's first two bytes
                haveFmt = true;
            }
            else if (id == "data")
            {
                if (!haveFmt)
                {
                    error = where("the data chunk comes before the fmt chunk", pos);
                    return false;
                }
                if (channels == 0 || channels > AudioLimits::sMaxChannels)
                {
                    error = where("unsupported channel count " + std::to_string(channels), pos);
                    return false;
                }
                if (rate == 0 || rate > AudioLimits::sMaxRate)
                {
                    error = where("unsupported sample rate " + std::to_string(rate), pos);
                    return false;
                }
                const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
                const bool floating = format == 3 && bits == 32;
                if (!pcm && !floating)
                {
                    error = where("unsupported sample format " + std::to_string(format) + "/" + std::to_string(bits)
                            + " bits (only PCM 8/16/24/32 and float 32 are read here)",
                        pos);
                    return false;
                }
                const std::uint64_t used = std::min(size, available);
                const std::size_t sampleBytes = bits / 8;
                const std::uint64_t frames = used / (sampleBytes * channels);
                if (frames == 0)
                {
                    error = where("the data chunk holds no samples", pos);
                    return false;
                }
                if (frames > AudioLimits::sMaxSamples)
                {
                    error = where("the audio is longer than the " + std::to_string(AudioLimits::sMaxSamples)
                            + " samples this editor keeps",
                        pos);
                    return false;
                }
                out.mRate = rate;
                out.mSamples.resize(static_cast<std::size_t>(frames));
                const unsigned char* p = data + body;
                for (std::size_t f = 0; f < frames; ++f)
                {
                    double sum = 0.0;
                    for (std::uint16_t c = 0; c < channels; ++c, p += sampleBytes)
                    {
                        double v = 0.0;
                        if (floating)
                        {
                            float x;
                            std::memcpy(&x, p, 4);
                            v = std::isfinite(x) ? x : 0.0;
                        }
                        else if (bits == 8)
                            v = (static_cast<int>(p[0]) - 128) / 128.0;
                        else if (bits == 16)
                            v = static_cast<std::int16_t>(le16(p)) / 32768.0;
                        else if (bits == 24)
                        {
                            std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                            if (s & 0x800000)
                                s -= 0x1000000;
                            v = s / 8388608.0;
                        }
                        else
                            v = static_cast<std::int32_t>(le32(p)) / 2147483648.0;
                        sum += v;
                    }
                    out.mSamples[f] = static_cast<float>(std::clamp(sum / channels, -1.0, 1.0));
                }
                return true;
            }
            const std::uint64_t next = static_cast<std::uint64_t>(body) + size + (size & 1);
            if (next > bytes.size())
                break;
            pos = static_cast<std::size_t>(next);
        }
        error = where(haveFmt ? "no data chunk" : "no fmt chunk", bytes.size());
        return false;
    }

    bool decodeVoiceBytes(std::string_view bytes, std::string_view name, Pcm& out, std::string& error)
    {
        out = Pcm();
        error.clear();
        if (bytes.empty())
        {
            error = std::string(name) + ": the file is empty";
            return false;
        }
        if (bytes.size() > AudioLimits::sMaxFileBytes)
        {
            error = std::string(name) + ": the file is larger than " + std::to_string(AudioLimits::sMaxFileBytes) + " bytes";
            return false;
        }
        if (isRiffWave(bytes))
        {
            std::string wavError;
            if (decodeWav(bytes, out, wavError))
                return true;
            // A WAV of a kind we do not read (ADPCM, ...) may still be one FFmpeg reads.
            std::string ffError;
            if (decodeWithFfmpeg(bytes, out, ffError))
                return true;
            error = std::string(name) + ": " + wavError;
            return false;
        }
        if (Fuz::hasFuzMagic(bytes))
        {
            const Fuz::Result split = Fuz::split(bytes, name);
            if (!split.mOk)
            {
                error = split.mError;
                return false;
            }
            std::string ffError;
            if (!decodeWithFfmpeg(split.mParts.mAudio, out, ffError))
            {
                error = std::string(name) + ": " + ffError;
                return false;
            }
            return true;
        }
        std::string ffError;
        if (!decodeWithFfmpeg(bytes, out, ffError))
        {
            error = std::string(name) + ": " + ffError;
            return false;
        }
        return true;
    }

    bool loadVoiceFile(const std::filesystem::path& path, Pcm& out, std::string& error)
    {
        out = Pcm();
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec)
        {
            error = path.generic_string() + ": cannot read the file (" + ec.message() + ")";
            return false;
        }
        if (size > AudioLimits::sMaxFileBytes)
        {
            error = path.generic_string() + ": the file is larger than " + std::to_string(AudioLimits::sMaxFileBytes)
                + " bytes";
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        {
            error = path.generic_string() + ": cannot read the file";
            return false;
        }
        return decodeVoiceBytes(bytes, path.generic_string(), out, error);
    }

    std::string encodeWav16(const std::vector<float>& samples, std::uint32_t rate)
    {
        std::string s;
        auto put32 = [&](std::uint32_t v) {
            for (int i = 0; i < 4; ++i)
                s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
        };
        auto put16 = [&](std::uint16_t v) {
            s.push_back(static_cast<char>(v & 0xff));
            s.push_back(static_cast<char>(v >> 8));
        };
        const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
        s += "RIFF";
        put32(36 + dataBytes);
        s += "WAVEfmt ";
        put32(16);
        put16(1);
        put16(1);
        put32(rate);
        put32(rate * 2);
        put16(2);
        put16(16);
        s += "data";
        put32(dataBytes);
        for (float v : samples)
            put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                std::lround(std::clamp(std::isfinite(v) ? v : 0.f, -1.f, 1.f) * 32767.f))));
        return s;
    }
}
