#ifndef OPENEDEN_RENDER_VRAMORDER_H
#define OPENEDEN_RENDER_VRAMORDER_H

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Render::VideoMemory
{
    /*
     * The `order` setting of [VideoMemory] (task 0080): a comma list of system names, first reduced first. The
     * settings reader has no list type, so it is one string split here. Pure.
     */

    struct ParsedOrder
    {
        std::vector<std::string> names; // known, in the user's order, without repeats
        std::vector<std::string> problems; // one message per name dropped (unknown or repeated)
    };

    inline std::string trimmedLower(std::string_view text)
    {
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;
        std::string result(text.substr(begin, end - begin));
        for (char& c : result)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    /// Splits `text` at commas, trims and lower-cases each name, keeps the names in `known` (in the order given) and
    /// reports the rest. An empty item is ignored without a message.
    inline ParsedOrder parseOrder(std::string_view text, const std::vector<std::string>& known)
    {
        ParsedOrder result;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t comma = text.find(',', start);
            if (comma == std::string_view::npos)
                comma = text.size();
            const std::string name = trimmedLower(text.substr(start, comma - start));
            start = comma + 1;
            if (name.empty())
                continue;
            if (std::find(known.begin(), known.end(), name) == known.end())
                result.problems.push_back("unknown system '" + name + "' ignored");
            else if (std::find(result.names.begin(), result.names.end(), name) != result.names.end())
                result.problems.push_back("system '" + name + "' listed twice, the second is ignored");
            else
                result.names.push_back(name);
        }
        return result;
    }

    /// The `budget` setting: `auto`, or a number of MiB.
    struct BudgetSetting
    {
        bool valid = true;
        bool autoBudget = true;
        long long mib = 0; // when not auto
    };

    inline BudgetSetting parseBudget(std::string_view text)
    {
        BudgetSetting result;
        const std::string value = trimmedLower(text);
        if (value.empty() || value == "auto")
            return result;
        long long mib = 0;
        for (const char c : value)
        {
            if (c < '0' || c > '9' || mib > 100000000)
            {
                result.valid = false;
                return result;
            }
            mib = mib * 10 + (c - '0');
        }
        if (mib <= 0)
        {
            result.valid = false;
            return result;
        }
        result.autoBudget = false;
        result.mib = mib;
        return result;
    }
}

#endif
