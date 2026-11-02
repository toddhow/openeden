// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include "utf8.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "format.hpp"

namespace EdenArchive
{
    bool isValidUtf8(std::string_view text) noexcept
    {
        std::size_t i = 0;
        const std::size_t n = text.size();
        while (i < n)
        {
            const auto b0 = static_cast<unsigned char>(text[i]);
            if (b0 < 0x80)
            {
                ++i;
                continue;
            }
            std::size_t length = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            if (b0 >= 0xC2 && b0 <= 0xDF)
            {
                length = 2;
                codePoint = b0 & 0x1Fu;
                minimum = 0x80;
            }
            else if (b0 >= 0xE0 && b0 <= 0xEF)
            {
                length = 3;
                codePoint = b0 & 0x0Fu;
                minimum = 0x800;
            }
            else if (b0 >= 0xF0 && b0 <= 0xF4)
            {
                length = 4;
                codePoint = b0 & 0x07u;
                minimum = 0x10000;
            }
            else
                return false;
            if (n - i < length)
                return false;
            for (std::size_t k = 1; k < length; ++k)
            {
                const auto b = static_cast<unsigned char>(text[i + k]);
                if ((b & 0xC0) != 0x80)
                    return false;
                codePoint = (codePoint << 6) | (b & 0x3Fu);
            }
            if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                return false;
            i += length;
        }
        return true;
    }

    ErrorClass checkStoredPath(std::string_view path, std::string& reason)
    {
        if (path.empty())
        {
            reason = "the path is empty";
            return ErrorClass::Limit;
        }
        if (path.size() > Format::maxPathLength)
        {
            reason = "the path is longer than 1024 bytes";
            return ErrorClass::Limit;
        }
        // The length limits first (they are limits); components are found without allocating.
        std::size_t start = 0;
        while (start <= path.size())
        {
            const std::size_t slash = path.find('/', start);
            const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
            if (end - start > Format::maxComponentLength)
            {
                reason = "a path component is longer than 255 bytes";
                return ErrorClass::Limit;
            }
            start = end + 1;
        }
        if (!isValidUtf8(path))
        {
            reason = "the path is not valid UTF-8";
            return ErrorClass::Name;
        }
        for (const char c : path)
        {
            if (static_cast<unsigned char>(c) < 0x20)
            {
                reason = "the path contains a control character";
                return ErrorClass::Name;
            }
            if (c == '\\')
            {
                reason = "the path contains a backslash";
                return ErrorClass::Name;
            }
            if (c >= 'A' && c <= 'Z')
            {
                reason = "the path contains an upper-case ASCII letter";
                return ErrorClass::Name;
            }
        }
        if (path.front() == '/' || path.back() == '/')
        {
            reason = "the path starts or ends with a slash";
            return ErrorClass::Name;
        }
        start = 0;
        while (start <= path.size())
        {
            const std::size_t slash = path.find('/', start);
            const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
            const std::string_view component = path.substr(start, end - start);
            if (component.empty())
            {
                reason = "the path has an empty component";
                return ErrorClass::Name;
            }
            if (component == "." || component == "..")
            {
                reason = "the path has a dot or dot-dot component";
                return ErrorClass::Name;
            }
            start = end + 1;
        }
        return ErrorClass::None;
    }

    int comparePaths(std::string_view a, std::string_view b) noexcept
    {
        const std::size_t common = std::min(a.size(), b.size());
        const int c = common == 0 ? 0 : std::memcmp(a.data(), b.data(), common);
        if (c != 0)
            return c < 0 ? -1 : 1;
        if (a.size() == b.size())
            return 0;
        return a.size() < b.size() ? -1 : 1;
    }
}
