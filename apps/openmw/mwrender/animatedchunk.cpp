#include "animatedchunk.hpp"

#include <algorithm>

namespace MWRender
{
    void AnimatedChunk::addObject(const vsg::dvec3& center, std::uint64_t id, const Controllers& controllers,
        const Controllers& skins, vsg::ref_ptr<vsg::Node> keepAlive)
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
        for (const auto& [skin, target] : skins)
            if (skin && target)
                object.skins.emplace_back(skin, target);
        object.keepAlive = std::move(keepAlive);

        Lod::AnimItem item;
        item.phase = Lod::animPhaseOf(id);
        std::size_t count = 0;
        for (const auto& group : object.groups)
            count += group.controllers.size();
        count += object.skins.size();
        item.controllers = static_cast<std::uint16_t>(std::min<std::size_t>(count, 0xffff));
        mItems.push_back(item);
        mObjects.push_back(std::move(object));
    }

    void AnimatedChunk::finish()
    {
        mRuns.reserve(mObjects.size());
    }

    std::size_t AnimatedChunk::skinCount() const
    {
        std::size_t count = 0;
        for (const Member& object : mObjects)
            count += object.skins.size();
        return count;
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
        // On the ground plane (stage e): the camera's height does not count.
        for (std::size_t i = 0; i < mObjects.size(); ++i)
            mItems[i].distance
                = Lod::animHorizontalDistance(mObjects[i].center.x - camera.x, mObjects[i].center.y - camera.y);
        mRuns.clear();
        Lod::planAnimFrame(mItems, dt, stopDistance, frame, budget, mRuns);
        unsigned ran = 0;
        for (const Lod::AnimRun& run : mRuns)
        {
            Member& object = mObjects[run.index];
            for (Anim::AutoPlay::Group& group : object.groups)
            {
                group.update(run.dt);
                ran += static_cast<unsigned>(group.controllers.size());
            }
            // The skins read the bones the groups just moved.
            for (const auto& [skin, target] : object.skins)
            {
                skin->run(*target, 0.f);
                ++ran;
            }
        }
        return ran;
    }

    std::size_t AnimatedChunk::countInTier(Lod::AnimTier tier) const
    {
        return static_cast<std::size_t>(std::count_if(
            mItems.begin(), mItems.end(), [tier](const Lod::AnimItem& item) { return item.tier == tier; }));
    }
}
