#include <game_skyrimse/actor_table.hpp>

#include <cctype>
#include <cmath>
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
        Row row;
        row.mHandle = info.handle;
        row.mBase = info.base;
        row.mPaces = !mTestWalk.empty() && lowerAscii(info.editorId) == mTestWalk;
        auto found = mRows.find(info.handle.reference);
        if (found != mRows.end())
        {
            // The same placement again with a new session (the cell reloaded before the old one was removed).
            if (found->second.mPaces)
                --mPacers;
            found->second = row;
        }
        else
            mRows.emplace(info.handle.reference, row);
        if (row.mPaces)
            ++mPacers;
        ++mStats.mAdded;
    }

    void SkyrimActorTable::onActorRemoved(openeden::sdk::game::ActorHandle handle)
    {
        auto found = mRows.find(handle.reference);
        if (found == mRows.end() || found->second.mHandle != handle)
            return; // already replaced by a newer session, or never held
        if (found->second.mPaces)
            --mPacers;
        mRows.erase(found);
        ++mStats.mRemoved;
    }

    void SkyrimActorTable::update(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        if (mPacers == 0)
            return;
        for (auto it = mRows.begin(); it != mRows.end();)
        {
            Row& row = it->second;
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
