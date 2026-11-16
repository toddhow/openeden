// Task 0250: summons and raise dead, the pure rules. See summons.hpp.
#include "game_skyrimse/summons.hpp"

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    SummonSpot summonSpawnSpot(double x, double y, double z, double heading, double distance, double rise)
    {
        SummonSpot spot;
        spot.x = x + std::sin(heading) * distance;
        spot.y = y + std::cos(heading) * distance;
        spot.z = z + rise;
        return spot;
    }

    float summonSeconds(std::uint32_t duration)
    {
        return duration > 0 ? static_cast<float>(duration) : kDefaultSummonSeconds;
    }

    bool reanimateLevelAllowed(std::uint16_t targetLevel, float magnitude)
    {
        if (!std::isfinite(magnitude) || magnitude <= 0.f)
            return true;
        return static_cast<float>(targetLevel) <= magnitude;
    }

    bool corpseInSights(double x, double y, double heading, double cx, double cy, double reach, double minDot)
    {
        const double dx = cx - x;
        const double dy = cy - y;
        const double distance = std::hypot(dx, dy);
        if (!std::isfinite(distance) || distance > reach)
            return false;
        if (distance < 1e-6)
            return true;
        const double dot = (std::sin(heading) * dx + std::cos(heading) * dy) / distance;
        return dot >= minDot;
    }

    bool SummonLedger::add(const SummonEntry& entry)
    {
        if (entry.mReference == openeden::sdk::game::kNoWorldId || mEntries.size() >= kMaxSummonEntries
            || find(entry.mReference) != nullptr)
            return false;
        mEntries.push_back(entry);
        return true;
    }

    SummonEntry* SummonLedger::find(openeden::sdk::game::WorldId reference)
    {
        for (SummonEntry& e : mEntries)
            if (e.mReference == reference)
                return &e;
        return nullptr;
    }

    const SummonEntry* SummonLedger::find(openeden::sdk::game::WorldId reference) const
    {
        for (const SummonEntry& e : mEntries)
            if (e.mReference == reference)
                return &e;
        return nullptr;
    }

    SummonEntry* SummonLedger::findByHandle(openeden::sdk::game::ActorHandle handle)
    {
        if (!handle.valid())
            return nullptr;
        for (SummonEntry& e : mEntries)
            if (e.mArrived && e.mHandle == handle)
                return &e;
        return nullptr;
    }

    std::size_t SummonLedger::countOf(const openeden::sdk::game::CombatSubject& master) const
    {
        return static_cast<std::size_t>(
            std::count_if(mEntries.begin(), mEntries.end(), [&](const SummonEntry& e) { return e.mMaster == master; }));
    }

    const SummonEntry* SummonLedger::oldestOf(const openeden::sdk::game::CombatSubject& master) const
    {
        for (const SummonEntry& e : mEntries)
            if (e.mMaster == master)
                return &e;
        return nullptr;
    }

    bool SummonLedger::erase(openeden::sdk::game::WorldId reference)
    {
        const auto it = std::find_if(
            mEntries.begin(), mEntries.end(), [&](const SummonEntry& e) { return e.mReference == reference; });
        if (it == mEntries.end())
            return false;
        mEntries.erase(it);
        return true;
    }

    std::vector<openeden::sdk::game::WorldId> SummonLedger::expired(float now) const
    {
        std::vector<openeden::sdk::game::WorldId> out;
        for (const SummonEntry& e : mEntries)
            if (now >= e.mExpiresAt)
                out.push_back(e.mReference);
        return out;
    }
}
