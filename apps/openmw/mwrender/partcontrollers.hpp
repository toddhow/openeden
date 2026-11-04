#ifndef OPENMW_MWRENDER_PARTCONTROLLERS_H
#define OPENMW_MWRENDER_PARTCONTROLLERS_H

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/animation/autoplay.hpp>

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
    // Removes the first (controller, target) link from `list`, if it is there (task 0074d).
    inline void unlinkController(Anim::Update& list, const Anim::Controller* controller, vsg::Object* target)
    {
        auto& links = list.controllers;
        const auto found = std::find(links.begin(), links.end(), std::make_pair(controller, target));
        if (found != links.end())
            links.erase(found);
    }

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
    // parts that stayed again. N is ESM::PRT_Count (an Npc), or 3 (a WieldingCreature).
    //
    // Threads (task 0074d, review of 0074b, finding 5): update(dt), the clocks, runs where its owner's update runs: an
    // NPC's or creature's on one of the vsgUtil::UpdateThreads workers (Scene::update gives each cell one operation;
    // [Cells] animation threads), the player's on the main thread (RenderManager), the inventory preview's on the main
    // thread with dt 0. Everything else (beginUpdate, beginPart, record, addAutoPlay, forget, relinkSurvivors,
    // runUpdate) runs on the main thread in the actor's part update, never while that actor's update runs: the main
    // thread waits for the workers (UpdateThreads::run, waitIdle). The state is per actor, so the workers share none.
    // Task 0074e (finding 3 of the review of 0074d): that includes the quiver change of weapon sheathing and its
    // compile (a failed compile removes every part, forget included): Npc::update only leaves a flag, and
    // Npc::updatePendingQuiver does the work on the main thread, after the workers are idle (Scene::update) or in the
    // player's update.
    //
    // Task 0074b: each part also owns its auto-play clock (autoPlay(part)): its groups and their timers live and die
    // with the part, as OpenMW 0.51 keeps a part's controllers with the part (PartHolder). An update that keeps the
    // part leaves its clock running; a part inserted again gets a fresh clock (group 0 at 0, a random timer for the
    // other groups), as every inserted part did before.
    //
    // Ownership: the controllers and their targets belong to the part's node (MWAnim::decorate clones them into it;
    // the owner holds its path). This holds a vsg::ref_ptr to each of them besides, so an entry and the part's clock
    // stay valid until beginPart or forget for that part, even if the part's node went another way: such a stale part
    // costs controller runs on a detached node, not a use-after-free. That covers the entries and the clocks only:
    // the owner's live lists (Anim::Update) hold raw pointers, so before forget(part) the owner takes the part's
    // entries out of them (Npc::detachPart, unlinkController: task 0074d, the first review's finding 7). The owner
    // calls forget(part) wherever the part's node goes, and beginPart(part) before linking a new node there.
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

        // The part is being linked (Npc::insertBoundedPart): its old entries and its old clock went with its old node.
        void beginPart(std::size_t part)
        {
            clearPart(part);
            mLinked.set(part);
        }

        void record(std::size_t part, const Anim::Controller* controller, vsg::Object* target, PartControllerList list)
        {
            mEntries[part].push_back(Entry{ controller, target, list });
            mHeld[part].emplace_back(controller);
            mHeld[part].emplace_back(target);
        }

        // An auto-play controller of the part: recorded, and run by the part's own clock in its phase group (no offset:
        // the groups are the part's own, so they never meet another file's).
        void addAutoPlay(std::size_t part, const Anim::Controller* controller, vsg::Object* target)
        {
            record(part, controller, target, PartControllerList::AutoPlay);
            mClocks[part].add(controller, target);
        }

        // The part's own auto-play clock, and all of them (indexed by part).
        const Anim::AutoPlay& autoPlay(std::size_t part) const { return mClocks[part]; }
        std::span<const Anim::AutoPlay> clocks() const { return mClocks; }

        // Runs every part's clock (the owner's update, every frame; allocates nothing).
        void update(float dt)
        {
            for (Anim::AutoPlay& clock : mClocks)
                clock.update(dt);
        }

        // One update of the parts (Npc::updateParts): beginUpdate, then `body` (which inserts and removes parts), then
        // `relink`, which links the parts that stayed again. `relink` runs also when `body` throws, and the exception
        // is then passed on: vsgUtil::readNode throws for a model it cannot read, and without the relink every part
        // kept would run no controller until the next update that succeeds.
        template <class Body, class Relink>
        void runUpdate(Body&& body, Relink&& relink)
        {
            beginUpdate();
            try
            {
                body();
            }
            catch (...)
            {
                relink();
                throw;
            }
            relink();
        }

        // The part's node goes (Npc::removeIndividualPart): its controllers must never run again.
        void forget(std::size_t part)
        {
            clearPart(part);
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
        void clearPart(std::size_t part)
        {
            mEntries[part].clear();
            mClocks[part].clear();
            mHeld[part].clear();
        }

        std::array<std::vector<Entry>, N> mEntries{};
        std::array<Anim::AutoPlay, N> mClocks{};
        // A reference to every controller and target of the part's entries (and so of its clock), held as long as
        // they are (finding 5 of the task 0074 review).
        std::array<std::vector<vsg::ref_ptr<const vsg::Object>>, N> mHeld{};
        std::bitset<N> mLinked;
    };
}

#endif
