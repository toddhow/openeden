#include "strictjson.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace Misc::StrictJson
{
    std::optional<nlohmann::json> parse(std::string_view text, const Limits& limits, ParseError& error)
    {
        error = ParseError();
        if (text.size() > limits.mMaxBytes)
        {
            error.mMessage = "the file is larger than the allowed " + std::to_string(limits.mMaxBytes) + " bytes";
            return std::nullopt;
        }
        std::vector<std::set<std::string>> keys;
        std::string problem;
        auto callback = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
            if (!problem.empty())
                return false;
            if (depth > limits.mMaxDepth)
            {
                problem = "nesting is deeper than " + std::to_string(limits.mMaxDepth);
                return false;
            }
            switch (event)
            {
                case nlohmann::json::parse_event_t::object_start:
                    keys.emplace_back();
                    break;
                case nlohmann::json::parse_event_t::object_end:
                    if (!keys.empty())
                        keys.pop_back();
                    break;
                case nlohmann::json::parse_event_t::key:
                    if (!keys.empty() && !keys.back().insert(parsed.get<std::string>()).second)
                    {
                        problem = "duplicate key \"" + parsed.get<std::string>() + "\"";
                        return false;
                    }
                    break;
                case nlohmann::json::parse_event_t::value:
                    if (parsed.is_number_float())
                    {
                        problem = "only integer numbers are allowed";
                        return false;
                    }
                    break;
                default:
                    break;
            }
            return true;
        };
        try
        {
            nlohmann::json result = nlohmann::json::parse(text.begin(), text.end(), callback, true);
            if (!problem.empty())
            {
                error.mMessage = problem;
                return std::nullopt;
            }
            return result;
        }
        catch (const nlohmann::json::parse_error& e)
        {
            error.mMessage = problem.empty() ? std::string(e.what()) : problem;
            const std::size_t at = std::min<std::size_t>(e.byte > 0 ? e.byte - 1 : 0, text.size());
            error.mLine = 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + at, '\n'));
        }
        catch (const std::exception& e)
        {
            error.mMessage = problem.empty() ? std::string(e.what()) : problem;
        }
        return std::nullopt;
    }

    std::string canonicalText(const nlohmann::json& value)
    {
        // nlohmann::json keeps object keys sorted (std::map).
        return value.dump(2, ' ', false) + "\n";
    }

    namespace
    {
        // RFC 3629: no overlong forms, no surrogates, nothing above U+10FFFF.
        bool isValidUtf8(std::string_view text)
        {
            std::size_t i = 0;
            while (i < text.size())
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                std::size_t extra = 0;
                std::uint32_t minimum = 0;
                std::uint32_t code = 0;
                if (c < 0x80)
                {
                    ++i;
                    continue;
                }
                if (c >= 0xc2 && c <= 0xdf)
                {
                    extra = 1;
                    minimum = 0x80;
                    code = c & 0x1f;
                }
                else if (c >= 0xe0 && c <= 0xef)
                {
                    extra = 2;
                    minimum = 0x800;
                    code = c & 0x0f;
                }
                else if (c >= 0xf0 && c <= 0xf4)
                {
                    extra = 3;
                    minimum = 0x10000;
                    code = c & 0x07;
                }
                else
                    return false;
                if (i + extra >= text.size())
                    return false;
                for (std::size_t k = 1; k <= extra; ++k)
                {
                    const unsigned char d = static_cast<unsigned char>(text[i + k]);
                    if ((d & 0xc0) != 0x80)
                        return false;
                    code = (code << 6) | (d & 0x3f);
                }
                if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                    return false;
                i += extra + 1;
            }
            return true;
        }
    }

    bool isPlainText(std::string_view text)
    {
        if (!isValidUtf8(text))
            return false;
        return std::none_of(
            text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
    }

    ObjectView::ObjectView(
        const nlohmann::json& value, std::string pointer, std::initializer_list<std::string_view> allowed)
        : mValue(value)
        , mPointer(std::move(pointer))
    {
        if (!value.is_object())
            throw FormatError{ mPointer, "expected an object" };
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(it.key())) == allowed.end())
                throw FormatError{ mPointer + "/" + it.key(), "unknown key" };
        }
    }

    std::string ObjectView::pointer(std::string_view key) const
    {
        return mPointer + "/" + std::string(key);
    }

    std::string ObjectView::str(std::string_view key, std::size_t maxBytes, bool required) const
    {
        const auto it = mValue.find(std::string(key));
        if (it == mValue.end())
        {
            if (required)
                throw FormatError{ pointer(key), "missing" };
            return {};
        }
        if (!it->is_string())
            throw FormatError{ pointer(key), "expected a string" };
        std::string result = it->get<std::string>();
        if (result.size() > maxBytes)
            throw FormatError{ pointer(key), "longer than " + std::to_string(maxBytes) + " bytes" };
        if (!isPlainText(result))
            throw FormatError{ pointer(key), "not valid UTF-8 text without control characters" };
        return result;
    }

    std::int64_t ObjectView::integer(std::string_view key, std::int64_t min, std::int64_t max) const
    {
        const auto it = mValue.find(std::string(key));
        if (it == mValue.end())
            throw FormatError{ pointer(key), "missing" };
        if (!it->is_number_integer())
            throw FormatError{ pointer(key), "expected an integer" };
        if (it->is_number_unsigned() && it->get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX))
            throw FormatError{ pointer(key), "out of range" };
        const std::int64_t v = it->get<std::int64_t>();
        if (v < min || v > max)
            throw FormatError{ pointer(key), "out of range " + std::to_string(min) + " to " + std::to_string(max) };
        return v;
    }

    std::int64_t ObjectView::integerOr(
        std::string_view key, std::int64_t min, std::int64_t max, std::int64_t fallback) const
    {
        return has(key) ? integer(key, min, max) : fallback;
    }

    bool ObjectView::boolean(std::string_view key, bool fallback) const
    {
        const auto it = mValue.find(std::string(key));
        if (it == mValue.end())
            return fallback;
        if (!it->is_boolean())
            throw FormatError{ pointer(key), "expected true or false" };
        return it->get<bool>();
    }

    const nlohmann::json& ObjectView::array(std::string_view key, std::size_t maxItems, bool required) const
    {
        static const nlohmann::json empty = nlohmann::json::array();
        const auto it = mValue.find(std::string(key));
        if (it == mValue.end())
        {
            if (required)
                throw FormatError{ pointer(key), "missing" };
            return empty;
        }
        if (!it->is_array())
            throw FormatError{ pointer(key), "expected an array" };
        if (it->size() > maxItems)
            throw FormatError{ pointer(key), "more than " + std::to_string(maxItems) + " items" };
        return *it;
    }

    const nlohmann::json& ObjectView::object(std::string_view key) const
    {
        const auto it = mValue.find(std::string(key));
        if (it == mValue.end())
            throw FormatError{ pointer(key), "missing" };
        return *it;
    }
}
