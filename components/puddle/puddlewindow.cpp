#include "puddlewindow.hpp"

namespace Puddle
{
    PuddleWindow::PuddleWindow(
        std::shared_ptr<const PuddlePack> pack, DigestFunction digest, WarnFunction warn, int radius)
        : mPack(std::move(pack))
        , mDigest(std::move(digest))
        , mWarn(std::move(warn))
        , mParameters(parameterDigest(mPack->manifest().mSettings))
        , mRadius(radius < 0 ? 0 : radius)
    {
    }

    void PuddleWindow::update(std::int32_t centreX, std::int32_t centreY)
    {
        std::set<std::pair<std::int32_t, std::int32_t>> wanted;
        for (std::int32_t dy = -mRadius; dy <= mRadius; ++dy)
            for (std::int32_t dx = -mRadius; dx <= mRadius; ++dx)
                wanted.emplace(centreX + dx, centreY + dy);
        {
            std::lock_guard lock(mMutex);
            for (auto it = mLoaded.begin(); it != mLoaded.end();)
                it = wanted.count(it->first) != 0 ? std::next(it) : mLoaded.erase(it);
        }
        for (const auto& cell : wanted)
        {
            {
                std::lock_guard lock(mMutex);
                if (mLoaded.count(cell) != 0 || mIgnored.count(cell) != 0)
                    continue;
            }
            if (!mPack->contains(cell.first, cell.second))
                continue;
            std::string problem;
            auto map = std::make_shared<CellMap>();
            if (Error error = mPack->readCell(cell.first, cell.second, *map))
                problem = "cannot be read (" + error.describe() + ")";
            else if (map->mParameterDigest != mParameters)
                problem = "was baked with other settings";
            else
            {
                const std::optional<Digest> now = mDigest ? mDigest(cell.first, cell.second) : std::nullopt;
                if (!now || *now != map->mInputDigest)
                    problem = "was baked from other land (bake the puddle map again)";
            }
            std::string warning;
            {
                std::lock_guard lock(mMutex);
                if (!problem.empty())
                {
                    mIgnored.insert(cell);
                    if (!mWarned)
                    {
                        mWarned = true;
                        warning = "Puddle map: cell (" + std::to_string(cell.first) + ", " + std::to_string(cell.second)
                            + ") " + problem + "; such cells keep the old puddles (this is said once).";
                    }
                }
                else
                    mLoaded.emplace(cell, std::move(map));
            }
            if (!warning.empty() && mWarn)
                mWarn(warning);
        }
    }

    std::shared_ptr<const CellMap> PuddleWindow::find(std::int32_t cellX, std::int32_t cellY) const
    {
        std::lock_guard lock(mMutex);
        const auto it = mLoaded.find({ cellX, cellY });
        return it == mLoaded.end() ? nullptr : it->second;
    }

    std::size_t PuddleWindow::loadedCount() const
    {
        std::lock_guard lock(mMutex);
        return mLoaded.size();
    }

    std::size_t PuddleWindow::ignoredCount() const
    {
        std::lock_guard lock(mMutex);
        return mIgnored.size();
    }
}
