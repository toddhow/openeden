#include "chunkfade.hpp"

#include <algorithm>

namespace Terrain
{
    bool boundsOverlap(const Bounds& a, const Bounds& b)
    {
        const vsg::vec2 aMax = a.max();
        const vsg::vec2 bMax = b.max();
        return a.min.x < bMax.x && aMax.x > b.min.x && a.min.y < bMax.y && aMax.y > b.min.y;
    }

    void ChunkFader::setDesired(std::span<const Bounds> desired, bool instant,
        const std::function<bool(const Bounds&)>& covered, const std::function<bool(const Bounds&)>& startsSolid)
    {
        if (instant)
            mEntries.clear();
        for (auto& [bounds, entry] : mEntries)
            entry.mWanted = false;
        for (const Bounds& bounds : desired)
        {
            auto [it, inserted] = mEntries.try_emplace(bounds);
            it->second.mWanted = true;
            if (inserted)
                it->second.mFade = (instant || (startsSolid && startsSolid(bounds))) ? 1.f : 0.f;
        }
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            if (!it->second.mWanted && covered && covered(it->first))
                it = mEntries.erase(it);
            else
                ++it;
        }
    }

    bool ChunkFader::step(float seconds, float fadeSeconds)
    {
        const float rate = fadeSeconds > 0.f ? seconds / fadeSeconds : 1.f;
        for (auto& [bounds, entry] : mEntries)
        {
            if (entry.mWanted)
                entry.mFade = std::min(1.f, entry.mFade + rate);
        }
        bool dropped = false;
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            Entry& entry = it->second;
            if (entry.mWanted)
            {
                ++it;
                continue;
            }
            // A leaving chunk is replaced when a wanted chunk overlaps it. It stays full until all of those are solid.
            bool replaced = false;
            bool replacementSolid = true;
            for (const auto& [otherBounds, other] : mEntries)
            {
                if (!other.mWanted || !boundsOverlap(it->first, otherBounds))
                    continue;
                replaced = true;
                if (other.mFade < 1.f)
                    replacementSolid = false;
            }
            if (replaced)
            {
                if (replacementSolid)
                {
                    it = mEntries.erase(it);
                    dropped = true;
                    continue;
                }
                entry.mFade = std::min(1.f, entry.mFade + rate);
            }
            else
            {
                entry.mFade -= rate;
                if (entry.mFade <= 0.f)
                {
                    it = mEntries.erase(it);
                    dropped = true;
                    continue;
                }
            }
            ++it;
        }
        return dropped;
    }

    float ChunkFader::fadeOf(const Bounds& bounds) const
    {
        const auto it = mEntries.find(bounds);
        return it == mEntries.end() ? 1.f : it->second.mFade;
    }

    ChunkShare ChunkFader::shareOf(const Bounds& bounds) const
    {
        const auto it = mEntries.find(bounds);
        if (it == mEntries.end())
            return ChunkShare{};
        const Entry& entry = it->second;
        if (entry.mWanted || entry.mFade < 1.f)
            return ChunkShare{ entry.mFade, false };
        // A solid leaving chunk: is it replaced completely?
        double covered = 0.0;
        float weakest = 1.f;
        for (const auto& [otherBounds, other] : mEntries)
        {
            if (!other.mWanted || !boundsOverlap(bounds, otherBounds))
                continue;
            const float width
                = std::min(bounds.max().x, otherBounds.max().x) - std::max(bounds.min.x, otherBounds.min.x);
            const float height
                = std::min(bounds.max().y, otherBounds.max().y) - std::max(bounds.min.y, otherBounds.min.y);
            covered += static_cast<double>(width) * static_cast<double>(height);
            weakest = std::min(weakest, other.mFade);
        }
        const double area = static_cast<double>(bounds.size) * static_cast<double>(bounds.size);
        if (covered < area * 0.9999)
            return ChunkShare{ entry.mFade, false };
        return ChunkShare{ weakest, true };
    }

    std::vector<Bounds> ChunkFader::leaving() const
    {
        std::vector<Bounds> result;
        for (const auto& [bounds, entry] : mEntries)
        {
            if (!entry.mWanted)
                result.push_back(bounds);
        }
        return result;
    }

    bool ChunkFader::sharesAreaWith(std::span<const Bounds> bounds) const
    {
        for (const auto& [held, entry] : mEntries)
        {
            for (const Bounds& other : bounds)
            {
                if (boundsOverlap(held, other))
                    return true;
            }
        }
        return false;
    }

    bool ChunkFader::active() const
    {
        return std::any_of(mEntries.begin(), mEntries.end(),
            [](const auto& item) { return !item.second.mWanted || item.second.mFade < 1.f; });
    }
}
