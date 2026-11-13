#include "groundcoverinstances.hpp"

#include <algorithm>
#include <cmath>

#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/readerscache.hpp>

#include "../mwworld/groundcoverstore.hpp"

namespace MWRender
{
    bool GroundcoverDensity::isInstanceEnabled()
    {
        if (mDensity >= 1.f)
            return true;
        mTotal += mDensity;
        if (mTotal < 1.f)
            return false;
        mTotal -= 1.f;
        return true;
    }

    bool isInGroundcoverChunk(float x, float y, const vsg::vec2& minBound, const vsg::vec2& maxBound)
    {
        const vsg::vec2 size = maxBound - minBound;
        if (size.x >= 1 && size.y >= 1)
            return true;
        const float cellX = x / ESM::Land::REAL_SIZE;
        const float cellY = y / ESM::Land::REAL_SIZE;
        if ((minBound.x > std::floor(minBound.x) && cellX < minBound.x)
            || (minBound.y > std::floor(minBound.y) && cellY < minBound.y)
            || (maxBound.x < std::ceil(maxBound.x) && cellX >= maxBound.x)
            || (maxBound.y < std::ceil(maxBound.y) && cellY >= maxBound.y))
            return false;
        return true;
    }

    std::vector<std::pair<int, int>> groundcoverCellsInRange(float x, float y, float distance)
    {
        return groundcoverCellsInRange(x, y, distance, static_cast<float>(ESM::Land::REAL_SIZE));
    }

    std::vector<std::pair<int, int>> groundcoverCellsInRange(float x, float y, float distance, float cellSize)
    {
        std::vector<std::pair<int, int>> cells;
        // Every cell whose closed square reaches within the distance on each axis, including one only touched.
        const float reach = std::max(distance, 0.f);
        const int firstX = static_cast<int>(std::ceil((x - reach) / cellSize)) - 1;
        const int lastX = static_cast<int>(std::floor((x + reach) / cellSize));
        const int firstY = static_cast<int>(std::ceil((y - reach) / cellSize)) - 1;
        const int lastY = static_cast<int>(std::floor((y + reach) / cellSize));
        for (int cellX = firstX; cellX <= lastX; ++cellX)
        {
            for (int cellY = firstY; cellY <= lastY; ++cellY)
            {
                const float dx = std::max({ cellX * cellSize - x, 0.f, x - (cellX + 1) * cellSize });
                const float dy = std::max({ cellY * cellSize - y, 0.f, y - (cellY + 1) * cellSize });
                if (dx * dx + dy * dy <= distance * distance)
                    cells.emplace_back(cellX, cellY);
            }
        }
        return cells;
    }

    GroundcoverInstances collectGroundcoverInstances(
        const MWWorld::GroundcoverStore& store, float density, float size, const vsg::vec2& center)
    {
        GroundcoverInstances instances;
        if (density <= 0.f)
            return instances;

        const vsg::vec2 minBound = center - vsg::vec2(size / 2.f, size / 2.f);
        const vsg::vec2 maxBound = center + vsg::vec2(size / 2.f, size / 2.f);
        GroundcoverDensity calculator(density);
        ESM::ReadersCache readers;
        const int startX = static_cast<int>(std::floor(center.x - size / 2.f));
        const int startY = static_cast<int>(std::floor(center.y - size / 2.f));
        for (int cellX = startX; cellX < startX + size; ++cellX)
        {
            for (int cellY = startY; cellY < startY + size; ++cellY)
            {
                ESM::Cell cell;
                store.initCell(cell, cellX, cellY);
                if (cell.mContextList.empty())
                    continue;

                calculator.reset();
                std::map<ESM::RefNum, ESM::CellRef> refs;
                for (std::size_t i = 0; i < cell.mContextList.size(); ++i)
                {
                    const std::size_t index = static_cast<std::size_t>(cell.mContextList[i].index);
                    const ESM::ReadersCache::BusyItem reader = readers.get(index);
                    cell.restore(*reader, i);
                    ESM::CellRef ref;
                    bool deleted = false;
                    while (cell.getNextRef(*reader, ref, deleted))
                    {
                        if (!deleted && refs.find(ref.mRefNum) == refs.end() && !calculator.isInstanceEnabled())
                            deleted = true;
                        if (!deleted && !isInGroundcoverChunk(ref.mPos.pos[0], ref.mPos.pos[1], minBound, maxBound))
                            deleted = true;
                        if (deleted)
                        {
                            refs.erase(ref.mRefNum);
                            continue;
                        }
                        refs[ref.mRefNum] = std::move(ref);
                    }
                }

                for (auto& [refNum, cellRef] : refs)
                {
                    const VFS::Path::NormalizedView model = store.getGroundcoverModel(cellRef.mRefID);
                    if (model.empty())
                        continue;
                    auto it = instances.find(model);
                    if (it == instances.end())
                        it = instances.emplace_hint(
                            it, VFS::Path::Normalized(model), std::vector<GroundcoverInstance>());
                    it->second.push_back({ cellRef.mPos, cellRef.mScale });
                }
            }
        }
        return instances;
    }
}
