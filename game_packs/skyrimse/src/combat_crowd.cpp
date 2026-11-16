#include <game_skyrimse/combat_crowd.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse::Crowd
{
    namespace
    {
        struct Ranked
        {
            Candidate mCandidate;
            double mDistanceSquared = 0.0;
        };

        bool before(const Ranked& a, const Ranked& b)
        {
            if (a.mDistanceSquared != b.mDistanceSquared)
                return a.mDistanceSquared < b.mDistanceSquared;
            if (a.mCandidate.mHandle.reference != b.mCandidate.mHandle.reference)
                return a.mCandidate.mHandle.reference < b.mCandidate.mHandle.reference;
            return a.mCandidate.mHandle.session < b.mCandidate.mHandle.session;
        }
    }

    Choice choose(std::vector<Candidate>& candidates, double px, double py, std::size_t hostile, std::size_t duels)
    {
        Choice choice;
        if (!std::isfinite(px) || !std::isfinite(py))
            return choice;
        hostile = std::min(hostile, kMaxPicked);
        duels = std::min(duels, kMaxPicked / 2);
        std::vector<Ranked> ranked;
        ranked.reserve(candidates.size());
        for (const Candidate& c : candidates)
        {
            if (!c.mHandle.valid() || !std::isfinite(c.mX) || !std::isfinite(c.mY))
                continue;
            const double dx = c.mX - px;
            const double dy = c.mY - py;
            const double squared = dx * dx + dy * dy;
            if (squared > kMaxDistance * kMaxDistance)
                continue;
            ranked.push_back({ c, squared });
        }
        std::sort(ranked.begin(), ranked.end(), before);
        const std::size_t hostiles = std::min(hostile, ranked.size());
        for (std::size_t i = 0; i < hostiles; ++i)
            choice.mHostile.push_back(ranked[i].mCandidate.mHandle);
        const std::size_t first = hostiles;
        const std::size_t wanted = std::min(2 * duels, ranked.size() - first);
        std::vector<char> paired(wanted, 0);
        for (std::size_t i = 0; i < wanted; ++i)
        {
            if (paired[i] != 0)
                continue;
            // The nearest unpaired actor to this one (ties: the earlier in the ranking, which is deterministic).
            std::size_t best = wanted;
            double bestSquared = 0.0;
            for (std::size_t j = i + 1; j < wanted; ++j)
            {
                if (paired[j] != 0)
                    continue;
                const double dx = ranked[first + i].mCandidate.mX - ranked[first + j].mCandidate.mX;
                const double dy = ranked[first + i].mCandidate.mY - ranked[first + j].mCandidate.mY;
                const double squared = dx * dx + dy * dy;
                if (best == wanted || squared < bestSquared)
                {
                    best = j;
                    bestSquared = squared;
                }
            }
            if (best == wanted)
                break; // an odd one out has no opponent
            paired[i] = paired[best] = 1;
            const auto a = ranked[first + i].mCandidate.mHandle;
            const auto b = ranked[first + best].mCandidate.mHandle;
            choice.mOpponents.emplace_back(a, b);
            choice.mOpponents.emplace_back(b, a);
        }
        return choice;
    }
}
