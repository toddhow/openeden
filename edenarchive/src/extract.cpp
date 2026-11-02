// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// Writing the entries of a pack to a folder, with the extraction rules of section 15.4 of the specification.
#include <edenarchive/extract.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "pathclash.hpp"
#include "utf8.hpp"

namespace EdenArchive
{
    namespace
    {
        Error makeError(ErrorClass errorClass, std::string field, std::string message)
        {
            Error error;
            error.mClass = errorClass;
            error.mField = std::move(field);
            error.mMessage = std::move(message);
            return error;
        }

        char lowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool isReservedDeviceName(std::string_view component)
        {
            const std::size_t dot = component.find('.');
            std::string_view stem = component.substr(0, dot);
            while (!stem.empty() && stem.back() == ' ')
                stem.remove_suffix(1);
            std::string lower;
            for (const char c : stem)
                lower += lowerAscii(c);
            if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul" || lower == "conin$"
                || lower == "conout$" || lower == "clock$")
                return true;
            if (lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")))
                return lower[3] >= '1' && lower[3] <= '9';
            if (lower.size() == 5 && (lower.starts_with("com") || lower.starts_with("lpt")))
            {
                // COM and LPT followed by a superscript 1, 2 or 3 (UTF-8: C2 B9, C2 B2, C2 B3).
                const auto b3 = static_cast<unsigned char>(lower[3]);
                const auto b4 = static_cast<unsigned char>(lower[4]);
                return b3 == 0xC2 && (b4 == 0xB9 || b4 == 0xB2 || b4 == 0xB3);
            }
            return false;
        }

        // Simple lower-casing of the common alphabets (Latin-1, Latin Extended-A, Greek, Cyrillic), enough to find
        // two names that a case-insensitive file system would treat as one in practice. Not a full Unicode fold.
        std::uint32_t foldCodePoint(std::uint32_t c)
        {
            if (c < 0x80)
                return (c >= 'A' && c <= 'Z') ? c + 32 : c;
            if ((c >= 0xC0 && c <= 0xDE && c != 0xD7))
                return c + 32;
            if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178
                && c != 0x17F)
            {
                if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
                    return (c % 2 == 1) ? c + 1 : c;
                return (c % 2 == 0) ? c + 1 : c;
            }
            if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
                return c + 32;
            if (c >= 0x410 && c <= 0x42F)
                return c + 32;
            if (c >= 0x400 && c <= 0x40F)
                return c + 80;
            return c;
        }

        std::string foldName(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            std::size_t i = 0;
            while (i < name.size())
            {
                const auto b0 = static_cast<unsigned char>(name[i]);
                std::uint32_t cp = b0;
                std::size_t length = 1;
                if (b0 >= 0xF0)
                {
                    length = 4;
                    cp = b0 & 0x07u;
                }
                else if (b0 >= 0xE0)
                {
                    length = 3;
                    cp = b0 & 0x0Fu;
                }
                else if (b0 >= 0xC0)
                {
                    length = 2;
                    cp = b0 & 0x1Fu;
                }
                if (i + length > name.size())
                    length = name.size() - i;
                for (std::size_t k = 1; k < length; ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(name[i + k]) & 0x3Fu);
                cp = foldCodePoint(cp);
                if (cp < 0x80)
                    out += static_cast<char>(cp);
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                i += length;
            }
            return out;
        }

        std::filesystem::path utf8Path(std::string_view text)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        // True for a symbolic link, and on Windows also for a junction or any other reparse point.
        bool isLinkLike(const std::filesystem::path& path)
        {
            std::error_code ec;
            const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
            if (!ec && status.type() == std::filesystem::file_type::symlink)
                return true;
#ifdef _WIN32
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return true;
#endif
            return false;
        }

        // Creates the folders of `relative` below `root` one by one, refusing a link anywhere on the way.
        Error makeFolders(const std::filesystem::path& root, std::string_view directoryPart)
        {
            std::filesystem::path current = root;
            std::size_t start = 0;
            while (start < directoryPart.size())
            {
                std::size_t slash = directoryPart.find('/', start);
                if (slash == std::string_view::npos)
                    slash = directoryPart.size();
                current /= utf8Path(directoryPart.substr(start, slash - start));
                std::error_code ec;
                const std::filesystem::file_status status = std::filesystem::symlink_status(current, ec);
                if (std::filesystem::exists(status))
                {
                    if (isLinkLike(current))
                        return makeError(
                            ErrorClass::Io, "path", "'" + current.string() + "' is a link; refusing to follow it");
                    if (!std::filesystem::is_directory(status))
                        return makeError(
                            ErrorClass::Io, "path", "'" + current.string() + "' exists and is not a folder");
                }
                else
                {
                    std::filesystem::create_directory(current, ec);
                    if (ec)
                        return makeError(
                            ErrorClass::Io, "path", "cannot create '" + current.string() + "': " + ec.message());
                }
                start = slash + 1;
            }
            return Error{};
        }
    }

    std::optional<std::string> checkExtractionSafe(std::string_view path)
    {
        if (path.empty())
            return "the name is empty";
        if (!isValidUtf8(path))
            return "the name is not valid UTF-8";
        for (const char c : path)
        {
            if (static_cast<unsigned char>(c) < 0x20)
                return "the name contains a control character";
        }
        auto isSeparator = [](char c) { return c == '/' || c == '\\'; };
        if (isSeparator(path.front()))
            return "the name starts with a separator";
        if (isSeparator(path.back()))
            return "the name ends with a separator";
        if (path.find('\\') != std::string_view::npos)
            return "the name contains a backslash";
        std::size_t start = 0;
        while (start <= path.size())
        {
            std::size_t end = start;
            while (end < path.size() && path[end] != '/')
                ++end;
            const std::string_view component = path.substr(start, end - start);
            if (component.empty())
                return "the name has an empty component";
            if (component == "." || component == "..")
                return "the name has a dot or dot-dot component";
            for (const char c : component)
            {
                if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
                    return "a component contains a character that is not allowed in a Windows file name";
            }
            if (component.back() == '.' || component.back() == ' ')
                return "a component ends with a dot or a space";
            if (isReservedDeviceName(component))
                return "a component is a Windows reserved device name";
            start = end + 1;
        }
        return std::nullopt;
    }

    Error unpack(
        const Archive& archive, const std::filesystem::path& outputFolder, const UnpackOptions& options) noexcept
    {
        try
        {
            const auto nameCheck = options.mNameCheck
                ? options.mNameCheck
                : std::function<std::optional<std::string>(std::string_view)>(
                    [](std::string_view name) { return checkExtractionSafe(name); });

            // Everything is checked before the first file is written.
            std::map<std::string, std::string> folded;
            for (std::size_t i = 0; i < archive.entryCount(); ++i)
            {
                const EntryInfo entry = archive.entry(i);
                if (!entry.mHasPath)
                    continue;
                if (const std::optional<std::string> problem = nameCheck(entry.mPath))
                    return makeError(ErrorClass::Name, "entry[" + std::to_string(i) + "].path",
                        "'" + std::string(entry.mPath) + "' is not safe to extract: " + *problem);
                const auto inserted = folded.emplace(foldName(entry.mPath), std::string(entry.mPath));
                if (!inserted.second)
                    return makeError(ErrorClass::Name, "entry[" + std::to_string(i) + "].path",
                        "'" + std::string(entry.mPath) + "' and '" + inserted.first->second
                            + "' would be one file on a case-insensitive file system");
            }
            std::size_t pathEntries = 0;
            while (pathEntries < archive.entryCount() && archive.entry(pathEntries).mHasPath)
                ++pathEntries;
            if (const std::optional<std::size_t> clash
                = findFileDirectoryClash(pathEntries, [&archive](std::size_t i) { return archive.entry(i).mPath; }))
                return makeError(ErrorClass::Name, "entry[" + std::to_string(*clash) + "].path",
                    "'" + std::string(archive.entry(*clash).mPath) + "' is both a file and a folder");

            std::error_code ec;
            std::filesystem::create_directories(outputFolder, ec);
            if (ec)
                return makeError(
                    ErrorClass::Io, "output", "cannot create '" + outputFolder.string() + "': " + ec.message());
            if (isLinkLike(outputFolder))
                return makeError(ErrorClass::Io, "output", "the output folder is a link");

            const std::uint64_t block = std::max<std::uint64_t>(archive.info().mChunkSize,
                (std::uint64_t{ 1 } << 20) / archive.info().mChunkSize * archive.info().mChunkSize);
            std::vector<std::uint8_t> buffer;
            for (std::size_t i = 0; i < archive.entryCount(); ++i)
            {
                const EntryInfo entry = archive.entry(i);
                if (!entry.mHasPath)
                    continue;
                const std::size_t lastSlash = entry.mPath.rfind('/');
                if (lastSlash != std::string_view::npos)
                {
                    if (Error error = makeFolders(outputFolder, entry.mPath.substr(0, lastSlash)))
                        return error;
                }
                const std::filesystem::path target = outputFolder / utf8Path(entry.mPath);
                std::error_code statusError;
                const std::filesystem::file_status status = std::filesystem::symlink_status(target, statusError);
                if (std::filesystem::exists(status))
                {
                    if (isLinkLike(target))
                        return makeError(
                            ErrorClass::Io, "path", "'" + target.string() + "' is a link; refusing to follow it");
                    if (!options.mOverwrite)
                        return makeError(ErrorClass::Io, "path", "'" + target.string() + "' exists; not overwriting");
                    if (std::filesystem::is_directory(status))
                        return makeError(ErrorClass::Io, "path", "'" + target.string() + "' is a folder");
                }
                std::filesystem::path temporary = target;
                temporary += ".edenunpack.tmp";
                if (std::filesystem::exists(std::filesystem::symlink_status(temporary, statusError))
                    && isLinkLike(temporary))
                    return makeError(
                        ErrorClass::Io, "path", "'" + temporary.string() + "' is a link; refusing to follow it");
                bool good = true;
                Hasher hasher;
                {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    if (!out)
                        return makeError(ErrorClass::Io, "path", "cannot create '" + temporary.string() + "'");
                    std::uint64_t done = 0;
                    while (done < entry.mSize && good)
                    {
                        const std::size_t n
                            = static_cast<std::size_t>(std::min<std::uint64_t>(block, entry.mSize - done));
                        buffer.resize(n);
                        if (const Error error = archive.readRange(i, done, n, buffer.data()))
                        {
                            out.close();
                            std::filesystem::remove(temporary, ec);
                            return error;
                        }
                        hasher.update(buffer.data(), n);
                        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(n));
                        good = out.good();
                        done += n;
                    }
                    out.close();
                    good = good && !out.fail();
                }
                if (!good)
                {
                    std::filesystem::remove(temporary, ec);
                    return makeError(ErrorClass::Io, "path", "writing '" + temporary.string() + "' failed");
                }
                const Hash hash = entry.mSize == 0 ? emptyHash() : hasher.finalize();
                if (hash != entry.mContentHash)
                {
                    std::filesystem::remove(temporary, ec);
                    return makeError(ErrorClass::Hash, "entry[" + std::to_string(i) + "]",
                        "the content of '" + std::string(entry.mPath) + "' does not match its hash");
                }
                std::filesystem::rename(temporary, target, ec);
                if (ec)
                {
                    std::filesystem::remove(temporary, ec);
                    return makeError(ErrorClass::Io, "path", "cannot write '" + target.string() + "'");
                }
            }
            return Error{};
        }
        catch (const std::bad_alloc&)
        {
            return makeError(ErrorClass::Limit, "memory", "out of memory");
        }
        catch (...)
        {
            return makeError(ErrorClass::Io, "unpack", "an unexpected exception while unpacking");
        }
    }
}
