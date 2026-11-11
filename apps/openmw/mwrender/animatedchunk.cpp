#include "animatedchunk.hpp"

#include <algorithm>

namespace MWRender
{
    void AnimatedChunk::addObject(const vsg::dvec3& center, std::uint64_t id, const Controllers& controllers)
    {
        if (controllers.empty())
            return;
        Member object;
        object.center = center;
        object.owned = controllers;
        for (const auto& [controller, target] : controllers)
        {
            if (!controller || !target)
                continue;
            // One group per phase group, as Anim::AutoPlay keeps them. Its start time for a group other than 0 is
            // random there; here it comes from the object id, so a chunk built twice runs the same way.
            const int phaseGroup = controller->hints.phaseGroup;
            auto group = std::find_if(object.groups.begin(), object.groups.end(),
                [phaseGroup](const Anim::AutoPlay::Group& g) { return g.phaseGroup == phaseGroup; });
            if (group == object.groups.end())
            {
                group = object.groups.emplace(object.groups.end());
                group->phaseGroup = phaseGroup;
                group->timer = Lod::animStartTime(id, phaseGroup);
            }
            group->controllers.emplace_back(controller.get(), target.get());
        }
        if (object.groups.empty())
            return;

        Lod::AnimItem item;
        item.phase = Lod::animPhaseOf(id);
        std::size_t count = 0;
        for (const auto& group : object.groups)
            count += group.controllers.size();
        item.controllers = static_cast<std::uint16_t>(std::min<std::size_t>(count, 0xffff));
        mItems.push_back(item);
        mObjects.push_back(std::move(object));
    }

    void AnimatedChunk::finish()
    {
        mRuns.reserve(mObjects.size());
    }

    std::size_t AnimatedChunk::controllerCount() const
    {
        std::size_t count = 0;
        for (const Member& object : mObjects)
            for (const auto& group : object.groups)
                count += group.controllers.size();
        return count;
    }

    unsigned AnimatedChunk::update(
        const vsg::dvec3& camera, double dt, float stopDistance, unsigned frame, Lod::AnimBudget& budget)
    {
        for (std::size_t i = 0; i < mObjects.size(); ++i)
            mItems[i].distance = static_cast<float>(vsg::length(mObjects[i].center - camera));
        mRuns.clear();
        Lod::planAnimFrame(mItems, dt, stopDistance, frame, budget, mRuns);
        unsigned ran = 0;
        for (const Lod::AnimRun& run : mRuns)
            for (Anim::AutoPlay::Group& group : mObjects[run.index].groups)
            {
                group.update(run.dt);
                ran += static_cast<unsigned>(group.controllers.size());
            }
        return ran;
    }

    std::size_t AnimatedChunk::countInTier(Lod::AnimTier tier) const
    {
        return static_cast<std::size_t>(
            std::count_if(mItems.begin(), mItems.end(), [tier](const Lod::AnimItem& item) { return item.tier == tier; }));
    }
}
