#include "cachestats.hpp"


namespace Resource
{
    namespace
    {
        std::string makeAttribute(std::string_view prefix, std::string_view suffix)
        {
            std::string result;
            result.reserve(prefix.size() + 1 + suffix.size());
            result += prefix;
            result += ' ';
            result += suffix;
            return result;
        }
    }

    void addCacheStatsAttibutes(std::string_view prefix, std::vector<std::string>& out)
    {
        constexpr std::string_view suffixes[] = {
            "Count",
            "Get",
            "Hit",
            "Expired",
        };

        for (std::string_view suffix : suffixes)
            out.push_back(makeAttribute(prefix, suffix));
    }

}
