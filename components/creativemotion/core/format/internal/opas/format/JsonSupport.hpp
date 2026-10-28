#pragma once

// Internal JSON helpers shared by every JSON loader and writer (rig, profile,
// marker, body-map, pose-clip and preset files). Not a public header: it is
// reached only through opas::format_json, which modules link PRIVATE, so
// nlohmann/json never appears in a public header (see core/format/CLAUDE.md).

#include "opas/format/LoadError.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace opas::format::detail {

using Json = nlohmann::json;

// nlohmann/json rejects a number too large for a double (e.g. 1e999) with an
// exception that carries no position (out_of_range 406). To still report the
// line and field, such literals are replaced by this string and the text is
// parsed again; the replacement keeps every line intact, and the readers
// below report it as "must be finite". The leading NUL (written \u0000) means
// no real file contains it.
inline constexpr std::string_view kNonFiniteSentinel = "\"\\u0000opas-non-finite-number\"";
// The decoded sentinel: a NUL byte, then the name (23 bytes in all).
inline constexpr std::string_view kNonFiniteValue("\0opas-non-finite-number", 23);

// How far an authored rotation may be from unit length (the same 1e-6 that
// validateSkeleton uses for rest rotations).
inline constexpr double kUnitQuatTolerance = 1e-6;

// Returns the text with every number literal that overflows a double replaced
// by kNonFiniteSentinel. String contents are skipped.
std::string replaceOverflowingNumbers(std::string_view text);

// JSON pointer escaping (RFC 6901): "~" -> "~0", "/" -> "~1".
std::string pointerToken(std::string_view key);

// Validates parsed JSON, remembering the first problem with its line. Every
// read function returns false once an error is recorded, so callers can
// simply stop. Format-specific readers derive from it.
class JsonReader {
public:
    JsonReader(std::string_view text, std::string_view file) : text_(text), file_(file) {}

    [[nodiscard]] const LoadError& error() const { return error_; }

    // Line (1-based) of a byte offset in the text.
    [[nodiscard]] std::size_t lineAt(std::size_t offset) const
    {
        const std::size_t end = std::min(offset, text_.size());
        std::size_t line = 1;
        for (std::size_t i = 0; i < end; ++i) {
            if (text_[i] == '\n') {
                ++line;
            }
        }
        return line;
    }

    bool failAtLine(std::size_t line, std::string field, std::string message)
    {
        error_ = LoadError{std::string(file_), line, std::move(field), std::move(message)};
        return false;
    }

    // Records an error at the line where `value` starts (positions are kept by
    // nlohmann/json's JSON_DIAGNOSTIC_POSITIONS).
    bool fail(const Json& value, std::string field, std::string message)
    {
        const std::size_t start = value.start_pos();
        return failAtLine(start == std::string::npos ? 1 : lineAt(start), std::move(field),
                          std::move(message));
    }

    // Rejects keys outside `allowed` (catches typos).
    bool onlyKeys(const Json& object, const std::string& pointer,
                  std::initializer_list<std::string_view> allowed)
    {
        for (const auto& [key, value] : object.items()) {
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
                return fail(value, pointer + "/" + pointerToken(key),
                            "unknown key \"" + key + "\"");
            }
        }
        return true;
    }

    // Finds a required member; a missing one is reported at the object's line.
    const Json* member(const Json& object, const std::string& pointer, std::string_view key)
    {
        const auto it = object.find(key);
        if (it == object.end()) {
            fail(object, pointer + "/" + std::string(key), "missing required field");
            return nullptr;
        }
        return &*it;
    }

    bool readString(const Json& value, const std::string& pointer, std::string& out)
    {
        if (!value.is_string() || value.get<std::string>() == kNonFiniteValue) {
            return fail(value, pointer, "must be a string");
        }
        out = value.get<std::string>();
        if (out.empty()) {
            return fail(value, pointer, "must not be empty");
        }
        return true;
    }

    bool readNumber(const Json& value, const std::string& pointer, double& out)
    {
        if (value.is_string() && value.get<std::string>() == kNonFiniteValue) {
            return fail(value, pointer, "must be finite");
        }
        if (!value.is_number()) {
            return fail(value, pointer, "must be a number");
        }
        out = value.get<double>();
        if (!std::isfinite(out)) {
            return fail(value, pointer, "must be finite");
        }
        return true;
    }

    bool readInt(const Json& value, const std::string& pointer, int& out)
    {
        if (!value.is_number_integer()) {
            return fail(value, pointer, "must be an integer");
        }
        if (value.is_number_unsigned()) {
            const auto v = value.get<std::uint64_t>();
            if (v > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return fail(value, pointer, "is out of range");
            }
            out = static_cast<int>(v);
            return true;
        }
        const auto v = value.get<std::int64_t>();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
            return fail(value, pointer, "is out of range");
        }
        out = static_cast<int>(v);
        return true;
    }

    bool readVec3(const Json& value, const std::string& pointer, math::Vec3& out)
    {
        if (!value.is_array() || value.size() != 3) {
            return fail(value, pointer, "must be an array of 3 numbers");
        }
        std::array<double, 3> v{};
        for (std::size_t i = 0; i < 3; ++i) {
            if (!readNumber(value[i], pointer + "/" + std::to_string(i), v[i])) {
                return false;
            }
        }
        out = math::Vec3{v[0], v[1], v[2]};
        return true;
    }

    // [x, y, z, w], at most kUnitQuatTolerance from unit length. Authored
    // rotations must be normalized; they are not silently fixed.
    bool readUnitQuat(const Json& value, const std::string& pointer, math::Quat& out)
    {
        if (!value.is_array() || value.size() != 4) {
            return fail(value, pointer, "must be an array of 4 numbers [x, y, z, w]");
        }
        std::array<double, 4> v{};
        for (std::size_t i = 0; i < 4; ++i) {
            if (!readNumber(value[i], pointer + "/" + std::to_string(i), v[i])) {
                return false;
            }
        }
        out = math::Quat{v[0], v[1], v[2], v[3]};
        if (!(std::abs(math::length(out) - 1.0) <= kUnitQuatTolerance)) {
            return fail(value, pointer, "must be a unit quaternion");
        }
        return true;
    }

    // The exact text a parsed value came from (positions are kept by
    // JSON_DIAGNOSTIC_POSITIONS), e.g. to read a number straight as float
    // without rounding twice through double. Empty if unknown.
    [[nodiscard]] std::string_view sourceText(const Json& value) const
    {
        const std::size_t start = value.start_pos();
        const std::size_t end = value.end_pos();
        if (start == std::string::npos || end == std::string::npos || end < start ||
            end > text_.size()) {
            return {};
        }
        return text_.substr(start, end - start);
    }

private:
    std::string_view text_;
    std::string_view file_;
    LoadError error_;
};

// Parses `text`, builds a ReaderT over it and runs `read(reader, root)`.
// `read` returns
// true on success. Syntax errors and any exception become a LoadError with a
// line; nothing is thrown. Returns the error, or nullopt on success.
template <typename ReaderT, typename Read>
std::optional<LoadError> parseAndRead(std::string_view text, std::string_view fileName, Read read)
{
    try {
        // `parsed` is the text nlohmann/json actually read: the input, or the
        // input with overflowing numbers replaced. Line numbers are counted in
        // it; replacements never change lines.
        std::string replaced;
        std::string_view parsed = text;
        Json root;
        try {
            try {
                root = Json::parse(parsed.begin(), parsed.end());
            } catch (const Json::out_of_range&) {
                replaced = replaceOverflowingNumbers(text);
                parsed = replaced;
                root = Json::parse(parsed.begin(), parsed.end());
            }
        } catch (const Json::parse_error& e) {
            JsonReader reader(parsed, fileName);
            // e.byte is the 1-based index of the last character read.
            const std::size_t offset = e.byte > 0 ? e.byte - 1 : 0;
            reader.failAtLine(reader.lineAt(offset), "", e.what());
            return reader.error();
        }
        ReaderT reader(parsed, fileName);
        if (read(reader, root)) {
            return std::nullopt;
        }
        return reader.error();
    } catch (const std::exception& e) {
        // Readers only read values whose types were checked, so this should
        // not happen; still, never let an exception escape.
        return LoadError{std::string(fileName), 1, "", std::string("internal error: ") + e.what()};
    }
}

// Reads a whole file. On failure returns a LoadError naming the file (as
// UTF-8). Never throws.
std::optional<std::string> readTextFile(const std::filesystem::path& path, LoadError& error);

// UTF-8 form of a path for error messages. path::string() can throw on
// Windows for names outside the system code page; u8string() cannot.
std::string utf8FileName(const std::filesystem::path& path);

// --- Writing ------------------------------------------------------------------

// Shortest text that reads back as exactly the same double.
std::string number(double value);
// A JSON string literal (quotes included), escaping what JSON requires.
std::string jsonString(std::string_view text);
// Shortest text that reads back as exactly the same float (recordings store
// physics floats). Negative zero is written "0", as number() does.
std::string floatNumber(float value);
// "[x, y, z]"
std::string vec3(const math::Vec3& v);
// "[x, y, z, w]"
std::string quat(const math::Quat& q);

} // namespace opas::format::detail
