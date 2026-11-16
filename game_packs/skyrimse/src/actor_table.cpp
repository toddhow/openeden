#include <game_skyrimse/actor_table.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace game_skyrimse
{
    namespace
    {
        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }
    }

    namespace
    {
        std::string_view trimmed(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            return text;
        }

        bool parseNumber(std::string_view text, double& out)
        {
            text = trimmed(text);
            if (text.empty())
                return false;
            const std::string copy(text); // strtod wants a terminated string; the parts are short
            char* end = nullptr;
            const double value = std::strtod(copy.c_str(), &end);
            if (end != copy.c_str() + copy.size() || !std::isfinite(value))
                return false;
            out = value;
            return true;
        }
    }

    bool parseTestGoto(std::string_view text, TestGoto& out)
    {
        std::string_view parts[4];
        std::size_t count = 0;
        while (count < 4)
        {
            const std::size_t comma = text.find(',');
            if (comma == std::string_view::npos)
            {
                parts[count++] = text;
                text = {};
                break;
            }
            parts[count++] = text.substr(0, comma);
            text.remove_prefix(comma + 1);
        }
        if (count != 4 || !text.empty())
            return false;
        const std::string_view name = trimmed(parts[0]);
        if (name.empty() || name.size() > kMaxActorTestWalkName)
            return false;
        TestGoto parsed;
        if (!parseNumber(parts[1], parsed.mX) || !parseNumber(parts[2], parsed.mY) || !parseNumber(parts[3], parsed.mZ))
            return false;
        parsed.mEditorId = lowerAscii(name);
        out = std::move(parsed);
        return true;
    }

    bool SkyrimActorTable::setTestGoto(std::string_view text)
    {
        mHasGoto = false;
        mTestGoto = {};
        if (trimmed(text).empty())
            return true;
        if (!parseTestGoto(text, mTestGoto))
            return false;
        mHasGoto = true;
        return true;
    }

    std::optional<openeden::sdk::game::GoalResult> SkyrimActorTable::goalResultOf(
        openeden::sdk::game::WorldId reference) const
    {
        const auto found = mRows.find(reference);
        return found == mRows.end() ? std::nullopt : found->second.mGoalResult;
    }

    void SkyrimActorTable::enablePackages(
        PackageSource& source, std::uint64_t seed, FurnitureSource* furniture, bool snapSleepers)
    {
        PackageBrain::Options options;
        options.mSeed = seed;
        options.mSnapSleepers = snapSleepers;
        mBrain = std::make_unique<PackageBrain>(source, options);
        mBrain->setFurnitureSource(furniture);
    }

    void SkyrimActorTable::onGoalFinished(
        openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result)
    {
        if (mBrain)
            mBrain->onGoalFinished(handle, result);
        const auto found = mRows.find(handle.reference);
        if (found == mRows.end() || found->second.mHandle != handle)
            return; // a stale result
        found->second.mGoalResult = result;
        switch (result)
        {
            case openeden::sdk::game::GoalResult::Arrived:
                ++mStats.mGoalsArrived;
                break;
            case openeden::sdk::game::GoalResult::NoPath:
                ++mStats.mGoalsNoPath;
                break;
            case openeden::sdk::game::GoalResult::Blocked:
                ++mStats.mGoalsBlocked;
                break;
        }
    }

    bool SkyrimActorTable::setTestWalk(std::string_view editorId)
    {
        if (editorId.size() > kMaxActorTestWalkName)
        {
            mTestWalk.clear();
            return false;
        }
        mTestWalk = lowerAscii(editorId);
        return true;
    }

    void SkyrimActorTable::onActorAdded(const openeden::sdk::game::ActorInfo& info)
    {
        if (!info.handle.valid())
            return;
        if (mBrain)
            mBrain->onActorAdded(info);
        if (mBrain && !mTestFollowerNames.empty() && mTestFollowerNames.count(lowerAscii(info.editorId)) != 0
            && !mTestDismissed)
        {
            // Task 0196: the test rule recruits an actor the moment it appears.
            mTestFollowerRefs.insert(info.handle.reference);
            (void)mBrain->setFollowPlayer(info.handle.reference, true);
            if (mTestFollowClock < 0.f)
                mTestFollowClock = 0.f;
        }
        Row row;
        row.mHandle = info.handle;
        row.mBase = info.base;
        row.mPaces = !mTestWalk.empty() && lowerAscii(info.editorId) == mTestWalk;
        row.mGoes = mHasGoto && lowerAscii(info.editorId) == mTestGoto.mEditorId;
        auto found = mRows.find(info.handle.reference);
        if (found != mRows.end())
        {
            // The same placement again with a new session (the cell reloaded before the old one was removed).
            if (found->second.mPaces)
                --mPacers;
            if (found->second.mGoes)
                --mGoers;
            found->second = row;
        }
        else
            mRows.emplace(info.handle.reference, row);
        if (row.mPaces)
            ++mPacers;
        if (row.mGoes)
            ++mGoers;
        ++mStats.mAdded;
    }

    bool SkyrimActorTable::setTestFollowers(std::string_view list)
    {
        mTestFollowerNames.clear();
        std::size_t begin = 0;
        while (begin <= list.size())
        {
            std::size_t end = list.find(',', begin);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view name = list.substr(begin, end - begin);
            while (!name.empty() && name.front() == ' ')
                name.remove_prefix(1);
            while (!name.empty() && name.back() == ' ')
                name.remove_suffix(1);
            if (!name.empty())
            {
                if (name.size() > kMaxActorTestWalkName || mTestFollowerNames.size() >= kMaxFollowers)
                {
                    mTestFollowerNames.clear();
                    return false;
                }
                mTestFollowerNames.insert(lowerAscii(name));
            }
            begin = end + 1;
        }
        return true;
    }

    bool SkyrimActorTable::recruit(openeden::sdk::game::WorldId reference)
    {
        if (!mBrain || mBrain->followerRefs().size() >= kMaxFollowers)
            return false;
        return mBrain->setFollowPlayer(reference, true);
    }

    bool SkyrimActorTable::setFollowerWaiting(openeden::sdk::game::WorldId reference, bool waiting)
    {
        return mBrain && mBrain->setFollowerWaiting(reference, waiting);
    }

    bool SkyrimActorTable::followerWaiting(openeden::sdk::game::WorldId reference) const
    {
        return mBrain && mBrain->followerWaiting(reference);
    }

    bool SkyrimActorTable::dismiss(openeden::sdk::game::WorldId reference)
    {
        return mBrain && mBrain->setFollowPlayer(reference, false);
    }

    openeden::sdk::game::WorldId SkyrimActorTable::homeCell(openeden::sdk::game::WorldId reference) const
    {
        const auto found = mHomes.find(reference);
        if (found != mHomes.end() && found->second != openeden::sdk::game::kNoWorldId)
            return found->second;
        if (!mHomeLookup)
            return openeden::sdk::game::kNoWorldId;
        const openeden::sdk::game::WorldId home = mHomeLookup(reference);
        if (home != openeden::sdk::game::kNoWorldId)
            mHomes[reference] = home;
        return home;
    }

    std::vector<FollowerEntry> SkyrimActorTable::followerEntries() const
    {
        std::vector<FollowerEntry> out;
        for (const openeden::sdk::game::WorldId id : followerIds())
            out.push_back({ id, homeCell(id) });
        return out;
    }

    bool SkyrimActorTable::restoreFollowers(const std::vector<FollowerEntry>& entries)
    {
        if (!mBrain)
            return false;
        std::vector<openeden::sdk::game::WorldId> ids;
        for (const FollowerEntry& e : entries)
        {
            ids.push_back(e.mId);
            if (e.mHome != openeden::sdk::game::kNoWorldId)
                mHomes[e.mId] = e.mHome;
        }
        const std::set<openeden::sdk::game::WorldId> wanted(ids.begin(), ids.end());
        const std::set<openeden::sdk::game::WorldId> current = mBrain->followerRefs();
        for (const auto id : current)
            if (wanted.count(id) == 0)
                (void)mBrain->setFollowPlayer(id, false);
        for (const auto id : wanted)
            (void)mBrain->setFollowPlayer(id, true);
        return true;
    }

    std::vector<openeden::sdk::game::WorldId> SkyrimActorTable::followerIds() const
    {
        if (!mBrain)
            return {};
        return std::vector<openeden::sdk::game::WorldId>(mBrain->followerRefs().begin(), mBrain->followerRefs().end());
    }

    std::size_t SkyrimActorTable::followers(openeden::sdk::game::WorldId* out, std::size_t capacity) const
    {
        if (!mBrain)
            return 0;
        std::size_t n = 0;
        for (const auto id : mBrain->followerRefs())
        {
            if (mBrain->followerWaiting(id))
                continue; // task 0239: a follower told to wait stays where it is when the player goes through a door
            if (out != nullptr && n < capacity)
                out[n] = id;
            ++n;
        }
        return n;
    }

    bool SkyrimActorTable::isFollower(openeden::sdk::game::WorldId reference) const
    {
        return mBrain && mBrain->followsPlayer(reference);
    }

    void SkyrimActorTable::onActorRemoved(openeden::sdk::game::ActorHandle handle)
    {
        if (mBrain)
            mBrain->onActorRemoved(handle);
        auto found = mRows.find(handle.reference);
        if (found == mRows.end() || found->second.mHandle != handle)
            return; // already replaced by a newer session, or never held
        if (found->second.mPaces)
            --mPacers;
        if (found->second.mGoes)
            --mGoers;
        mRows.erase(found);
        ++mStats.mRemoved;
    }

    void SkyrimActorTable::update(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        if (mBrain)
            mBrain->update(seconds, writer);
        if (mTestFollowClock >= 0.f && mTestDismissAfter > 0.f && !mTestDismissed && mBrain)
        {
            mTestFollowClock += seconds;
            if (mTestFollowClock >= mTestDismissAfter)
            {
                mTestDismissed = true;
                for (const auto id : mTestFollowerRefs)
                    (void)mBrain->setFollowPlayer(id, false);
            }
        }
        if (mPacers == 0 && mGoers == 0)
            return;
        for (auto it = mRows.begin(); it != mRows.end();)
        {
            Row& row = it->second;
            if (row.mGoes && !row.mGoalIssued)
            {
                const openeden::sdk::game::ActorGoal goal{ mTestGoto.mX, mTestGoto.mY, mTestGoto.mZ,
                    openeden::sdk::game::ActorSpeed::Walk };
                if (!writer.goTo(row.mHandle, goal))
                {
                    ++mStats.mRequestsRefused;
                    if (row.mPaces)
                        --mPacers;
                    --mGoers;
                    it = mRows.erase(it);
                    continue;
                }
                row.mGoalIssued = true;
                ++mStats.mGoalsIssued;
            }
            if (!row.mPaces)
            {
                ++it;
                continue;
            }
            row.mElapsed += seconds;
            // East for the first half period, west for the second, and so on. The time only ever comes from the core's
            // frame time, never a clock.
            const auto halves = static_cast<std::uint64_t>(std::floor(row.mElapsed / kPaceHalfPeriodSeconds));
            const int direction = (halves % 2 == 0) ? 1 : -1;
            if (direction != row.mDirection)
            {
                if (!writer.setMovement(row.mHandle, direction * kPaceSpeed, 0.f))
                {
                    // The core does not know this handle (any more): forget the row, it will be told again if the actor
                    // comes back.
                    ++mStats.mRequestsRefused;
                    --mPacers;
                    it = mRows.erase(it);
                    continue;
                }
                row.mDirection = direction;
                ++mStats.mRequestsWritten;
            }
            ++it;
        }
    }
}
