#ifndef OPENEDEN_APPS_EDENARCHIVE_TOOLCOMMON_H
#define OPENEDEN_APPS_EDENARCHIVE_TOOLCOMMON_H

// Shared helpers of the Eden Archive command-line tools (edenpack, edenunpack, edenlist, edenvalidate).
// Exit codes of every tool: 0 success or valid pack, 1 invalid pack, failed read or refused operation, 2 usage or
// input/output error.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <edenarchive/archive.hpp>

namespace EdenTools
{
    inline constexpr int exitOk = 0;
    inline constexpr int exitInvalid = 1;
    inline constexpr int exitUsage = 2;

    /// Runs a tool body; an exception (out of memory, a name that cannot be converted) is exit code 2, not an abort.
    inline int runGuarded(int (*body)(int, char**), int argc, char** argv) noexcept
    {
        try
        {
            return body(argc, argv);
        }
        catch (...)
        {
            std::fprintf(stderr, "the tool stopped on an unexpected error\n");
            return exitUsage;
        }
    }

    /// Parses a non-negative decimal number; false if the text is empty, has other characters or overflows.
    inline bool parseUnsigned(std::string_view text, std::uint64_t& value)
    {
        if (text.empty())
            return false;
        std::uint64_t result = 0;
        for (const char c : text)
        {
            if (c < '0' || c > '9')
                return false;
            const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
            if (result > (~std::uint64_t{ 0 } - digit) / 10)
                return false;
            result = result * 10 + digit;
        }
        value = result;
        return true;
    }

    /// Parses 64 hexadecimal digits into a digest.
    inline bool parseHash(std::string_view text, EdenArchive::Hash& hash)
    {
        if (text.size() != 64)
            return false;
        for (std::size_t i = 0; i < 32; ++i)
        {
            int value = 0;
            for (std::size_t k = 0; k < 2; ++k)
            {
                const char c = text[i * 2 + k];
                int digit = 0;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    return false;
                value = value * 16 + digit;
            }
            hash[i] = static_cast<std::uint8_t>(value);
        }
        return true;
    }

    inline std::filesystem::path pathFromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    inline std::string pathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    /// Opens a pack for reading; on failure prints the located error and returns nullptr and sets `exitCode`
    /// (2 when the file cannot be read, 1 when it is not a valid pack).
    inline std::unique_ptr<EdenArchive::Archive> openPack(const std::filesystem::path& path, int& exitCode)
    {
        EdenArchive::Error error;
        std::unique_ptr<EdenArchive::Archive> pack
            = EdenArchive::Archive::open(path, EdenArchive::OpenOptions{}, error);
        if (!pack)
        {
            std::fprintf(stderr, "%s\n", error.describe(pathToUtf8(path)).c_str());
            exitCode = error.mClass == EdenArchive::ErrorClass::Io ? exitUsage : exitInvalid;
        }
        return pack;
    }

    /// One line per entry in entry order: "<size> <content hash> <path>" or "<size> <hash> key:<16 hex digits>".
    /// With `provenance`, each line also ends with " prov <tool id> <64 hex source digest> <parameter bytes in hex, or
    /// ->", or " prov none" for an entry without a record.
    inline void listPack(const EdenArchive::Archive& pack, bool provenance = false)
    {
        for (std::size_t i = 0; i < pack.entryCount(); ++i)
        {
            const EdenArchive::EntryInfo entry = pack.entry(i);
            const std::string hash = EdenArchive::toHex(entry.mContentHash);
            if (entry.mHasPath)
                std::printf("%llu %s %.*s", static_cast<unsigned long long>(entry.mSize), hash.c_str(),
                    static_cast<int>(entry.mPath.size()), entry.mPath.data());
            else
                std::printf("%llu %s key:%016llx", static_cast<unsigned long long>(entry.mSize), hash.c_str(),
                    static_cast<unsigned long long>(entry.mKey));
            if (provenance)
            {
                const std::optional<EdenArchive::ProvenanceInfo> record = pack.provenance(i);
                if (!record)
                    std::printf(" prov none");
                else
                {
                    std::string parameters;
                    for (const std::uint8_t byte : record->mParameters)
                    {
                        constexpr char digits[] = "0123456789abcdef";
                        parameters += digits[byte >> 4];
                        parameters += digits[byte & 15];
                    }
                    std::printf(" prov %.*s %s %s", static_cast<int>(record->mToolId.size()), record->mToolId.data(),
                        EdenArchive::toHex(record->mSourceDigest).c_str(),
                        parameters.empty() ? "-" : parameters.c_str());
                }
            }
            std::printf("\n");
        }
    }
}

#endif
