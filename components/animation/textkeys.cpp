#include "textkeys.hpp"

#include <string>
#include <vector>

#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/misc/strings/lower.hpp>

namespace Anim
{
    void addTextKeys(Tags& tags, float time, std::string_view text)
    {
        std::vector<std::string> results;
        Misc::StringUtils::split(text, results, "\r\n");
        for (std::string& result : results)
        {
            Misc::StringUtils::trim(result);
            Misc::StringUtils::lowerCaseInPlace(result);
            if (!result.empty())
                tags.emplace(time, std::move(result));
        }
    }

    bool addTextKeyLine(Tags& tags, std::string_view text)
    {
        std::string line(text);
        Misc::StringUtils::trim(line);
        const std::size_t spacePos = line.find_last_of(' ');
        if (spacePos == std::string::npos)
            return false;
        const float time = Misc::StringUtils::toNumeric<float>(std::string_view(line).substr(spacePos + 1), 0.f);
        std::string key = line.substr(0, spacePos);
        Misc::StringUtils::trim(key);
        Misc::StringUtils::lowerCaseInPlace(key);
        if (key.empty())
            return false;
        tags.emplace(time, std::move(key));
        return true;
    }
}
