#include "opas/format/JsonSupport.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace opas::format::detail {

std::string replaceOverflowingNumbers(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '"') {
            // Copy the whole string literal, honoring backslash escapes.
            const std::size_t start = i++;
            while (i < text.size() && text[i] != '"') {
                i += text[i] == '\\' ? 2 : 1;
            }
            i = std::min(i + 1, text.size());
            out.append(text.substr(start, i - start));
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = i;
            while (i < text.size() &&
                   std::string_view("+-0123456789.eE").find(text[i]) != std::string_view::npos) {
                ++i;
            }
            const std::string literal(text.substr(start, i - start));
            const double value = std::strtod(literal.c_str(), nullptr);
            if (std::isfinite(value)) {
                out += literal;
            } else {
                out += kNonFiniteSentinel;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// JSON pointer escaping (RFC 6901): "~" -> "~0", "/" -> "~1".
std::string pointerToken(std::string_view key)
{
    std::string token;
    for (const char c : key) {
        if (c == '~') {
            token += "~0";
        } else if (c == '/') {
            token += "~1";
        } else {
            token += c;
        }
    }
    return token;
}

std::string number(double value)
{
    // Shortest text that reads back as exactly the same double. Negative zero
    // is written "0": nlohmann/json reads "-0" as the integer 0, so "-0" would
    // not survive a load-write round trip byte for byte.
    if (value == 0.0) {
        return "0";
    }
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

std::string floatNumber(float value)
{
    if (value == 0.0f) {
        return "0";
    }
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

std::string jsonString(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // Other control characters must be \u escapes in JSON.
                constexpr std::string_view hex = "0123456789abcdef";
                const auto u = static_cast<unsigned char>(c);
                out += "\\u00";
                out += hex[u >> 4U];
                out += hex[u & 0x0FU];
            } else {
                out += c; // UTF-8 bytes pass through unchanged
            }
        }
    }
    out += '"';
    return out;
}

std::string vec3(const math::Vec3& v)
{
    return "[" + number(v.x) + ", " + number(v.y) + ", " + number(v.z) + "]";
}

std::string quat(const math::Quat& q)
{
    return "[" + number(q.x) + ", " + number(q.y) + ", " + number(q.z) + ", " + number(q.w) + "]";
}

std::string utf8FileName(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::optional<std::string> readTextFile(const std::filesystem::path& path, LoadError& error)
{
    const std::string fileName = utf8FileName(path);
    try {
        const std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = LoadError{fileName, 1, "", "cannot open file"};
            return std::nullopt;
        }
        std::ostringstream contents;
        contents << in.rdbuf();
        return contents.str();
    } catch (const std::exception& e) {
        // e.g. out of memory reading a huge file; loaders promise not to throw.
        error = LoadError{fileName, 1, "", std::string("cannot read file: ") + e.what()};
        return std::nullopt;
    }
}

} // namespace opas::format::detail
