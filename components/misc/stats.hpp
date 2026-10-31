#ifndef OPENEDEN_COMPONENTS_MISC_STATS_H
#define OPENEDEN_COMPONENTS_MISC_STATS_H

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Misc
{
    /// Numbers reported per frame by name, in place of osg::Stats and the part of it reportStats callers used (task 0013).
    class Stats
    {
    public:
        void setAttribute(unsigned int frameNumber, std::string_view name, double value)
        {
            mFrames[frameNumber].insert_or_assign(std::string(name), value);
        }

        std::optional<double> getAttribute(unsigned int frameNumber, std::string_view name) const
        {
            const auto frame = mFrames.find(frameNumber);
            if (frame == mFrames.end())
                return std::nullopt;
            const auto attribute = frame->second.find(name);
            if (attribute == frame->second.end())
                return std::nullopt;
            return attribute->second;
        }

    private:
        std::map<unsigned int, std::map<std::string, double, std::less<>>> mFrames;
    };
}

#endif
