#ifndef OPENMW_MWRENDER_PARTCONTROLLERS_H
#define OPENMW_MWRENDER_PARTCONTROLLERS_H

#include <array>
#include <bitset>
#include <cstddef>
#include <vector>

namespace Anim
{
    class Controller;
}

namespace vsg
{
    class Object;
}

namespace MWRender
{
    // Which of the Npc's lists a part's controller runs in (Npc::insertBoundedPart decides).
    enum class PartControllerList
    {
        Static,
        Face,
        Weapon,
        AutoPlay
    };

    // Task 0074: Npc::updateParts empties every controller list, then adds again only the parts it inserts; a body
    // part, the head or the hair that stays (priority 1, nothing displaced it) kept no controller and froze in the pose
    // of that moment. This remembers, per part type, what Npc::insertBoundedPart linked, so updateParts can link the
    // parts that stayed again. Main thread only, like the Npc. N is ESM::PRT_Count.
    template <std::size_t N>
    class PartControllers
    {
    public:
        struct Entry
        {
            const Anim::Controller* mController;
            vsg::Object* mTarget;
            PartControllerList mList;
        };

        // The lists were emptied (Npc::updateParts): no part is linked any more.
        void beginUpdate() { mLinked.reset(); }

        // The part is being linked (Npc::insertBoundedPart): its old entries went with its old node.
        void beginPart(std::size_t part)
        {
            mEntries[part].clear();
            mLinked.set(part);
        }

        void record(std::size_t part, const Anim::Controller* controller, vsg::Object* target, PartControllerList list)
        {
            mEntries[part].push_back(Entry{ controller, target, list });
        }

        // The part's node goes (Npc::removeIndividualPart): its controllers must never run again.
        void forget(std::size_t part)
        {
            mEntries[part].clear();
            mLinked.reset(part);
        }

        // Calls link(part, entries) for every part that still has entries and was not linked since beginUpdate, in
        // part order; the entries keep the order they were recorded in.
        template <class Link>
        void relinkSurvivors(Link&& link)
        {
            for (std::size_t part = 0; part < N; ++part)
            {
                if (mLinked.test(part) || mEntries[part].empty())
                    continue;
                link(part, mEntries[part]);
                mLinked.set(part);
            }
        }

        const std::vector<Entry>& entries(std::size_t part) const { return mEntries[part]; }

    private:
        std::array<std::vector<Entry>, N> mEntries{};
        std::bitset<N> mLinked;
    };
}

#endif
