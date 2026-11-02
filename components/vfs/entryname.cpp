#include "entryname.hpp"

#include <array>
#include <cstdint>

#include "pathutil.hpp"

namespace VFS::Path
{
    namespace
    {
        std::optional<std::string> checkShape(std::string_view name, bool separatorIsBackslashToo)
        {
            if (name.empty())
                return "the name is empty";
            if (!isValidUtf8(name))
                return "the name is not valid UTF-8";
            for (const char c : name)
            {
                if (static_cast<unsigned char>(c) < 0x20)
                    return "the name contains a control character";
            }

            auto isSeparator = [&](char c) { return c == '/' || (separatorIsBackslashToo && c == '\\'); };
            if (isSeparator(name.front()))
                return "the name starts with a separator";
            if (isSeparator(name.back()))
                return "the name ends with a separator";

            std::size_t start = 0;
            while (start <= name.size())
            {
                std::size_t end = start;
                while (end < name.size() && !isSeparator(name[end]))
                    ++end;
                const std::string_view component = name.substr(start, end - start);
                if (component.empty())
                    return "the name has an empty component";
                if (component == "." || component == "..")
                    return "the name has a '.' or '..' component";
                start = end + 1;
            }
            return std::nullopt;
        }

        bool isReservedDeviceName(std::string_view component)
        {
            const std::size_t dot = component.find('.');
            std::string_view stem = component.substr(0, dot);
            // Windows ignores spaces between the name and the dot: "con .txt" is CON.
            while (!stem.empty() && stem.back() == ' ')
                stem.remove_suffix(1);

            std::string lower;
            for (const char c : stem)
                lower += Misc::StringUtils::toLower(c);

            if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul" || lower == "conin$"
                || lower == "conout$" || lower == "clock$")
                return true;
            if (lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")))
                return lower[3] >= '1' && lower[3] <= '9';
            // COM and LPT followed by a superscript 1, 2 or 3 (UTF-8: C2 B9, C2 B2, C2 B3) are reserved since
            // Windows 10.
            if (lower.size() == 5 && (lower.starts_with("com") || lower.starts_with("lpt")))
            {
                const auto b3 = static_cast<unsigned char>(lower[3]);
                const auto b4 = static_cast<unsigned char>(lower[4]);
                return b3 == 0xC2 && (b4 == 0xB9 || b4 == 0xB2 || b4 == 0xB3);
            }
            return false;
        }
    }

    bool isValidUtf8(std::string_view text)
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
                codePoint = b0 & 0x1F;
                minimum = 0x80;
            }
            else if (b0 >= 0xE0 && b0 <= 0xEF)
            {
                length = 3;
                codePoint = b0 & 0x0F;
                minimum = 0x800;
            }
            else if (b0 >= 0xF0 && b0 <= 0xF4)
            {
                length = 4;
                codePoint = b0 & 0x07;
                minimum = 0x10000;
            }
            else
                return false; // a continuation byte on its own, 0xC0, 0xC1 or 0xF5 and up

            if (n - i < length)
                return false;
            for (std::size_t k = 1; k < length; ++k)
            {
                const auto b = static_cast<unsigned char>(text[i + k]);
                if ((b & 0xC0) != 0x80)
                    return false;
                codePoint = (codePoint << 6) | (b & 0x3F);
            }
            if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                return false;
            i += length;
        }
        return true;
    }

    std::optional<std::string> checkPackEntryName(std::string_view name, std::size_t maxLength)
    {
        if (name.size() > maxLength)
            return "the name is longer than " + std::to_string(maxLength) + " bytes";
        if (std::optional<std::string> problem = checkShape(name, false))
            return problem;
        if (!isNormalized(name))
            return "the name is not normalized (lower case, '/' as separator)";
        return std::nullopt;
    }

    std::optional<std::string> checkExtractionSafe(std::string_view name)
    {
        // A backslash is a separator on Windows, so it is split on here, and then rejected as a character below.
        if (std::optional<std::string> problem = checkShape(name, true))
            return problem;

        if (name.find('\\') != std::string_view::npos)
            return "the name contains a backslash";

        std::size_t start = 0;
        while (start <= name.size())
        {
            std::size_t end = start;
            while (end < name.size() && name[end] != '/' && name[end] != '\\')
                ++end;
            const std::string_view component = name.substr(start, end - start);

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
}
