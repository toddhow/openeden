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

    void SkyrimActorTable::onGoalFinished(
        openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result)
    {
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

    void SkyrimActorTable::onActorRemoved(openeden::sdk::game::ActorHandle handle)
    {
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
