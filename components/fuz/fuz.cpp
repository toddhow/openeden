#include "fuz.hpp"

namespace Fuz
{
    namespace
    {
        constexpr std::size_t kHeaderBytes = 12; // "FUZE", version, lip size
        constexpr std::size_t kRiffHeaderBytes = 12; // "RIFF", size, form type
        constexpr std::uint32_t kVersion = 1; // the only value in the game's data

        std::uint32_t readLe32(std::string_view bytes, std::size_t at)
        {
            std::uint32_t v = 0;
            for (std::size_t i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
            return v;
        }

        Result fail(std::string_view name, std::uint64_t offset, const std::string& what)
        {
            Result r;
            r.mError = (name.empty() ? std::string("fuz") : std::string(name)) + ": " + what + " (offset "
                + std::to_string(offset) + ")";
            return r;
        }
    }

    bool hasFuzMagic(std::string_view bytes)
    {
        return bytes.size() >= 4 && bytes.substr(0, 4) == "FUZE";
    }

    Result split(std::string_view bytes, std::string_view name, const Limits& limits)
    {
        if (bytes.size() > limits.mMaxBytes)
            return fail(name, 0, "the file is " + std::to_string(bytes.size()) + " bytes, over the limit of "
                    + std::to_string(limits.mMaxBytes));
        if (bytes.size() < kHeaderBytes)
            return fail(name, 0, "shorter than the 12 byte header");
        if (!hasFuzMagic(bytes))
            return fail(name, 0, "does not start with FUZE");
        const std::uint32_t version = readLe32(bytes, 4);
        if (version != kVersion)
            return fail(name, 4, "version " + std::to_string(version) + ", only 1 is known");
        const std::uint32_t lipSize = readLe32(bytes, 8);
        if (lipSize > limits.mMaxLipBytes)
            return fail(name, 8, "the lip part is " + std::to_string(lipSize) + " bytes, over the limit of "
                    + std::to_string(limits.mMaxLipBytes));
        const std::size_t audioAt = kHeaderBytes + static_cast<std::size_t>(lipSize);
        if (audioAt > bytes.size())
            return fail(name, 8, "the lip part of " + std::to_string(lipSize) + " bytes runs past the end of the file");

        const std::string_view audio = bytes.substr(audioAt);
        if (audio.size() < kRiffHeaderBytes)
            return fail(name, audioAt, "the audio part is shorter than a RIFF header");
        if (audio.substr(0, 4) != "RIFF")
            return fail(name, audioAt, "the audio part is not a RIFF file");
        const std::uint32_t riffSize = readLe32(audio, 4);
        if (riffSize < 4)
            return fail(name, audioAt + 4, "the RIFF size is " + std::to_string(riffSize) + ", less than its form type");
        const std::uint64_t riffTotal = static_cast<std::uint64_t>(riffSize) + 8;
        if (riffTotal > audio.size())
            return fail(name, audioAt + 4,
                "the RIFF size says " + std::to_string(riffTotal) + " bytes, " + std::to_string(audio.size())
                    + " are present");

        Result r;
        r.mOk = true;
        r.mParts.mLip = bytes.substr(kHeaderBytes, lipSize);
        r.mParts.mAudio = audio.substr(0, static_cast<std::size_t>(riffTotal));
        r.mParts.mAudioIsXwma = audio.substr(8, 4) == "XWMA";
        r.mParts.mTrailingBytes = audio.size() - riffTotal;
        return r;
    }
}
