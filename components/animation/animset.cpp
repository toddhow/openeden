#include "animset.hpp"

#include <utility>

namespace Anim
{
    namespace
    {
        vsg::ref_ptr<const Tags> tagsOf(const ControllerMap* controllers)
        {
            if (controllers != nullptr && controllers->tags != nullptr)
                return vsg::ref_ptr<const Tags>(controllers->tags.get());
            return vsg::ref_ptr<const Tags>(new Tags);
        }
    }

    AnimSet::AnimSet(vsg::ref_ptr<const ControllerMap> in_controllers, std::string in_name)
        : name(std::move(in_name))
        , controllers(std::move(in_controllers))
        , tags(tagsOf(controllers.get()))
    {
        // One pass in time order. The group of a key is the text before its first ": ", as Tags::emplace names it; a
        // key with no ": " names no group. A later marker overwrites an earlier one, so each holds the last.
        for (const auto& [time, text] : *tags)
        {
            const std::size_t separator = text.find(": ");
            if (separator == std::string::npos)
                continue;
            Group& group = mGroups[text.substr(0, separator)];
            if (group.firstKey == sAbsent)
                group.firstKey = time;
            const std::string_view marker = std::string_view(text).substr(separator + 2);
            if (marker == "start")
                group.start = time;
            else if (marker.starts_with("stop"))
                group.stop = time;
            else if (marker == "loop start")
                group.loopStart = time;
            else if (marker == "loop stop")
                group.loopStop = time;
        }
    }

    const AnimSet::Group* AnimSet::find(std::string_view group) const
    {
        const auto found = mGroups.find(group);
        return found != mGroups.end() ? &found->second : nullptr;
    }

    float AnimSet::startTime(std::string_view group) const
    {
        const Group* found = find(group);
        return found != nullptr ? found->firstKey : sAbsent;
    }

    float AnimSet::textKeyTime(std::string_view prefix) const
    {
        for (const auto& [time, text] : *tags)
            if (std::string_view(text).starts_with(prefix))
                return time;
        return sAbsent;
    }
}
