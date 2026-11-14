#include "neutralactorbridge.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics
{
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::ActorInfo;
    using openeden::sdk::game::ActorRules;
    using openeden::sdk::game::GoalResult;
    using openeden::sdk::game::WorldId;

    NeutralActorBridge::NeutralActorBridge()
        : mWriter(*this)
    {
        mPendingGoals.reserve(kMaxActors);
    }

    ActorHandle NeutralActorBridge::add(ActorRules* rules, const void* key, WorldId reference, WorldId base,
        std::string_view editorId, double x, double y, double z, float heading)
    {
        if (key == nullptr || reference == openeden::sdk::game::kNoWorldId || mEntries.size() >= kMaxActors
            || mNextSession == 0)
            return {};
        for (const Entry& entry : mEntries)
        {
            if (entry.mKey == key)
                return {};
        }
        ActorHandle handle;
        handle.reference = reference;
        handle.session = mNextSession++;
        mEntries.push_back({ handle, key }); // ascending sessions keep the table sorted
        ++mStats.mAdded;
        if (rules != nullptr)
        {
            ActorInfo info;
            info.handle = handle;
            info.base = base;
            info.editorId = editorId;
            info.x = x;
            info.y = y;
            info.z = z;
            info.heading = heading;
            try
            {
                rules->onActorAdded(info);
            }
            catch (...)
            {
                ++mStats.mRulesFailures; // nothing crosses the core / pack boundary
            }
        }
        return handle;
    }

    void NeutralActorBridge::remove(ActorRules* rules, const void* key)
    {
        const auto found
            = std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& entry) { return entry.mKey == key; });
        if (found == mEntries.end())
            return;
        const ActorHandle handle = found->mHandle;
        std::erase_if(mPendingGoals, [&](const PendingGoal& pending) { return pending.mKey == key; });
        if (rules != nullptr)
        {
            try
            {
                rules->onActorRemoved(handle);
            }
            catch (...)
            {
                ++mStats.mRulesFailures;
            }
        }
        mEntries.erase(found);
        ++mStats.mRemoved;
    }

    void NeutralActorBridge::clear()
    {
        mEntries.clear();
        mPendingGoals.clear();
    }

    void NeutralActorBridge::reportGoal(const void* key, GoalResult result)
    {
        if (key == nullptr || handleOf(key).session == 0)
        {
            ++mStats.mResultsDropped;
            return;
        }
        mPendingGoals.push_back({ key, result });
    }

    void NeutralActorBridge::setClock(const openeden::sdk::game::ActorClock& clock)
    {
        mClock = clock;
        if (!std::isfinite(clock.hour) || clock.hour < 0.f || clock.hour >= 24.f || clock.day < 0)
            mClock = openeden::sdk::game::ActorClock{};
    }

    void NeutralActorBridge::update(ActorRules* rules, float seconds, NeutralRequestSink& sink)
    {
        if (rules == nullptr || mEntries.empty())
        {
            mStats.mResultsDropped += mPendingGoals.size();
            mPendingGoals.clear();
            return;
        }
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;
        // Results of frame N-1 first, so the pack sees them before it decides what to ask for in frame N. A pack that
        // asks for something from inside onGoalFinished is refused: the writer is only open during update().
        for (const PendingGoal& pending : mPendingGoals)
        {
            const ActorHandle handle = handleOf(pending.mKey);
            if (!handle.valid())
            {
                ++mStats.mResultsDropped;
                continue;
            }
            try
            {
                rules->onGoalFinished(handle, pending.mResult);
                ++mStats.mResultsDelivered;
            }
            catch (...)
            {
                ++mStats.mRulesFailures;
            }
        }
        mPendingGoals.clear();
        mWriter.mSink = &sink;
        try
        {
            rules->update(seconds, mWriter);
        }
        catch (...)
        {
            ++mStats.mRulesFailures;
        }
        mWriter.mSink = nullptr;
    }

    ActorHandle NeutralActorBridge::handleOf(const void* key) const
    {
        if (key == nullptr)
            return {};
        for (const Entry& entry : mEntries)
        {
            if (entry.mKey == key)
                return entry.mHandle;
        }
        return {};
    }

    const void* NeutralActorBridge::Writer::keyOf(ActorHandle handle)
    {
        if (mSink == nullptr)
        {
            ++mBridge.mStats.mOutsideUpdate;
            return nullptr;
        }
        // Sessions are ascending in the table: binary search, then the placement must match as well.
        const auto found = std::lower_bound(mBridge.mEntries.begin(), mBridge.mEntries.end(), handle.session,
            [](const Entry& entry, std::uint32_t session) { return entry.mHandle.session < session; });
        if (handle.session == 0 || found == mBridge.mEntries.end() || found->mHandle != handle)
        {
            ++mBridge.mStats.mStale;
            return nullptr;
        }
        return found->mKey;
    }

    bool NeutralActorBridge::Writer::setMovement(ActorHandle handle, float velX, float velY)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(velX) || !std::isfinite(velY))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        ++mBridge.mStats.mRequests;
        mSink->setRequest(key, velX, velY);
        return true;
    }

    bool NeutralActorBridge::Writer::goTo(ActorHandle handle, const openeden::sdk::game::ActorGoal& goal)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)
            || static_cast<unsigned>(goal.speed) > static_cast<unsigned>(openeden::sdk::game::ActorSpeed::Run))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        ++mBridge.mStats.mGoals;
        mSink->setGoal(key, goal);
        return true;
    }

    bool NeutralActorBridge::Writer::cancelGoal(ActorHandle handle)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        ++mBridge.mStats.mCancels;
        mSink->cancelGoal(key);
        return true;
    }

    bool NeutralActorBridge::Writer::setHeading(ActorHandle handle, float heading)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(heading))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        ++mBridge.mStats.mHeadings;
        mSink->setHeading(key, heading);
        return true;
    }

    namespace
    {
        bool validClipName(std::string_view clip)
        {
            if (clip.empty() || clip.size() > openeden::sdk::game::kMaxFurnitureClipName)
                return false;
            return std::all_of(clip.begin(), clip.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }
    }

    bool NeutralActorBridge::Writer::useFurniture(ActorHandle handle, const openeden::sdk::game::FurnitureUse& use)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(use.x) || !std::isfinite(use.y) || !std::isfinite(use.z) || !std::isfinite(use.heading)
            || (use.pose != openeden::sdk::game::FurniturePose::Sit
                && use.pose != openeden::sdk::game::FurniturePose::Sleep)
            || !validClipName(use.clip))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        ++mBridge.mStats.mFurnitureUses;
        mSink->useFurniture(key, use);
        return true;
    }

    bool NeutralActorBridge::Writer::leaveFurniture(ActorHandle handle, double x, double y, double z)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        ++mBridge.mStats.mFurnitureLeaves;
        mSink->leaveFurniture(key, x, y, z);
        return true;
    }

    bool NeutralActorBridge::Writer::placeFurniture(
        ActorHandle handle, double x, double y, double z, float heading)
    {
        const void* key = keyOf(handle);
        if (key == nullptr)
            return false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(heading))
        {
            ++mBridge.mStats.mBadValues;
            return false;
        }
        if (!mSink->placeSeated(key, x, y, z, heading))
            return false;
        ++mBridge.mStats.mPlacements;
        return true;
    }

    bool NeutralActorBridge::Writer::actorPosition(ActorHandle handle, double& x, double& y, double& z) const
    {
        // A read: no counters, no refusal count. Only while update() runs (the sink is set), only for a live handle.
        if (mSink == nullptr || !handle.valid())
            return false;
        for (const Entry& entry : mBridge.mEntries)
            if (entry.mHandle == handle)
                return mSink->positionOf(entry.mKey, x, y, z);
        return false;
    }
}
