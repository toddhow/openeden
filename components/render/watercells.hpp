#ifndef OPENEDEN_RENDER_WATERCELLS_H
#define OPENEDEN_RENDER_WATERCELLS_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Task 0129: the bookkeeping of cell-bounded water surfaces (the neutral exteriors of a game pack, such as Skyrim SE).
// One surface is a square of `size` units at the grid square (x, y) (south-west corner x * size, y * size) at one
// height. Pure: no scene graph, no GPU; MWRender::Water mirrors this set into nodes. Main thread only.

namespace Render
{
    class WaterCells
    {
    public:
        struct Cell
        {
            std::int32_t mX = 0;
            std::int32_t mY = 0;
            float mSize = 0.f;
            float mHeight = 0.f;
        };

        /// Adds the surface of a grid square or replaces the one already there. A size that is not a positive finite
        /// number, or a height that is not finite, is refused. True when the set holds the surface afterwards.
        bool set(std::int32_t x, std::int32_t y, float size, float height)
        {
            if (!(size > 0.f) || !std::isfinite(size) || !std::isfinite(height))
                return false;
            if (Cell* existing = findMutable(x, y))
            {
                existing->mSize = size;
                existing->mHeight = height;
                return true;
            }
            mCells.push_back(Cell{ x, y, size, height });
            return true;
        }

        /// True when there was a surface to remove.
        bool remove(std::int32_t x, std::int32_t y)
        {
            for (auto it = mCells.begin(); it != mCells.end(); ++it)
            {
                if (it->mX == x && it->mY == y)
                {
                    mCells.erase(it);
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] const Cell* find(std::int32_t x, std::int32_t y) const
        {
            for (const Cell& c : mCells)
                if (c.mX == x && c.mY == y)
                    return &c;
            return nullptr;
        }

        [[nodiscard]] std::size_t size() const { return mCells.size(); }
        [[nodiscard]] bool empty() const { return mCells.empty(); }

        /// True when (px, py) is inside the square of some surface (south-west edges inside, north-east edges outside)
        /// and pz is below that surface's height.
        [[nodiscard]] bool isUnderwater(float px, float py, float pz) const
        {
            for (const Cell& c : mCells)
            {
                const float x0 = static_cast<float>(c.mX) * c.mSize;
                const float y0 = static_cast<float>(c.mY) * c.mSize;
                if (px >= x0 && px < x0 + c.mSize && py >= y0 && py < y0 + c.mSize && pz < c.mHeight)
                    return true;
            }
            return false;
        }

    private:
        Cell* findMutable(std::int32_t x, std::int32_t y)
        {
            for (Cell& c : mCells)
                if (c.mX == x && c.mY == y)
                    return &c;
            return nullptr;
        }

        std::vector<Cell> mCells;
    };
}

#endif
