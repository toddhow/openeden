#ifndef OPENEDEN_COMPONENTS_MISC_STRICTJSON_H
#define OPENEDEN_COMPONENTS_MISC_STRICTJSON_H

// A strict reader for small JSON files that OpenEden writes itself (tasks 0057 and 0058): a size cap, a nesting cap,
// duplicate keys refused, no floating point numbers, and a canonical writer (sorted keys, two-space indent, one
// trailing newline) so that read then write gives the same bytes. Internal helper: the nlohmann type appears in this
// header, so only .cpp files include it.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace Misc::StrictJson
{
    struct ParseError
    {
        std::size_t mLine = 1;
        std::string mMessage;
    };

    struct Limits
    {
        std::size_t mMaxBytes = 4u * 1024 * 1024;
        int mMaxDepth = 12;
    };

    /// Never throws. Returns nullopt and fills `error` for malformed text, text over the caps, a duplicate key or a
    /// number that is not an integer.
    std::optional<nlohmann::json> parse(std::string_view text, const Limits& limits, ParseError& error);

    /// Sorted keys, two-space indent, "\n" ends, one trailing newline, UTF-8 left as it is.
    std::string canonicalText(const nlohmann::json& value);

    /// Thrown by ObjectView and caught by the format readers, which turn it into their located error.
    struct FormatError
    {
        std::string mPointer;
        std::string mMessage;
    };

    /// Typed access to one JSON object. Every failure throws FormatError with a JSON pointer.
    class ObjectView
    {
    public:
        /// Throws unless `value` is an object whose keys are all in `allowed`.
        ObjectView(const nlohmann::json& value, std::string pointer, std::initializer_list<std::string_view> allowed);

        std::string pointer(std::string_view key) const;
        bool has(std::string_view key) const { return mValue.contains(std::string(key)); }
        std::string str(std::string_view key, std::size_t maxBytes, bool required = true) const;
        std::int64_t integer(std::string_view key, std::int64_t min, std::int64_t max) const;
        std::int64_t integerOr(std::string_view key, std::int64_t min, std::int64_t max, std::int64_t fallback) const;
        bool boolean(std::string_view key, bool fallback) const;
        const nlohmann::json& array(std::string_view key, std::size_t maxItems, bool required = true) const;
        const nlohmann::json& object(std::string_view key) const;

    private:
        const nlohmann::json& mValue;
        std::string mPointer;
    };

    /// Valid UTF-8 and no control characters (tab and newline are refused too).
    bool isPlainText(std::string_view text);
}

#endif
