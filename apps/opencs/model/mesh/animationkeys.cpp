#include "animationkeys.hpp"

#include <algorithm>
#include <cctype>

namespace CSMMesh
{
    std::vector<AnimationGroup> groupTextKeys(const std::vector<TextKey>& keys)
    {
        std::vector<AnimationGroup> groups;
        for (const TextKey& key : keys)
        {
            const std::size_t separator = key.text.find(": ");
            if (separator == std::string::npos)
                continue;
            const std::string name = key.text.substr(0, separator);
            auto it
                = std::find_if(groups.begin(), groups.end(), [&](const AnimationGroup& g) { return g.name == name; });
            if (it == groups.end())
            {
                groups.emplace_back();
                groups.back().name = name;
                it = groups.end() - 1;
            }
            it->keys.push_back(key);
        }
        for (AnimationGroup& group : groups)
        {
            std::stable_sort(group.keys.begin(), group.keys.end(),
                [](const TextKey& a, const TextKey& b) { return a.time < b.time; });
            group.start = group.keys.front().time;
            group.stop = group.keys.back().time;
        }
        return groups;
    }

    std::string kfSidecarFor(std::string_view modelPath)
    {
        const std::size_t slash = modelPath.find_last_of("\\/");
        const std::string_view base = slash == std::string_view::npos ? modelPath : modelPath.substr(slash + 1);
        if (base.size() < 5 || (base[0] != 'x' && base[0] != 'X'))
            return {};
        std::string extension(base.substr(base.size() - 4));
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".nif")
            return {};
        std::string result(modelPath.substr(0, modelPath.size() - 4));
        result += ".kf";
        return result;
    }
}
