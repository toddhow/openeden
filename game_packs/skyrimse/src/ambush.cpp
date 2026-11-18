#include <game_skyrimse/ambush.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse::ambush
{
    bool inRange(const Sleeper& sleeper, const Player& player, const Rules& rules)
    {
        if (!player.mKnown)
            return false;
        const double dx = player.mX - sleeper.mX;
        const double dy = player.mY - sleeper.mY;
        const double dz = player.mZ - sleeper.mZ;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
            return false;
        return std::hypot(dx, dy) <= rules.mDistance && std::abs(dz) <= rules.mHeight;
    }

    std::uint8_t aggressionFromValue(double value)
    {
        if (std::isnan(value) || value <= 0.0)
            return 0;
        if (value >= 3.0)
            return 3;
        return static_cast<std::uint8_t>(std::lround(value));
    }

    std::size_t Watch::update(
        std::span<const Sleeper> sleepers, const Player& player, std::vector<openeden::sdk::game::WorldId>& wake)
    {
        if (!player.mKnown)
            return 0;
        const std::size_t first = wake.size();
        for (const Sleeper& sleeper : sleepers)
            if (sleeper.mReference != openeden::sdk::game::kNoWorldId && mWoken.count(sleeper.mReference) == 0
                && inRange(sleeper, player, mRules))
                wake.push_back(sleeper.mReference);
        std::sort(wake.begin() + static_cast<std::ptrdiff_t>(first), wake.end());
        wake.erase(std::unique(wake.begin() + static_cast<std::ptrdiff_t>(first), wake.end()), wake.end());
        if (wake.size() - first > kMaxWakesPerUpdate)
            wake.resize(first + kMaxWakesPerUpdate);
        for (std::size_t i = first; i < wake.size(); ++i)
            if (mWoken.size() < kMaxRemembered)
                mWoken.insert(wake[i]);
        return wake.size() - first;
    }
}
