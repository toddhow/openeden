#ifndef OPENEDEN_TESTS_PUDDLETESTLAND_H
#define OPENEDEN_TESTS_PUDDLETESTLAND_H

// Generated land for the puddle bake tests (task 0095, stage W-b): a height function over global vertices and a set of
// cells that have land. The small cell (1024 units, 5 vertices a side, 256 units between vertices) at 64 units a texel
// gives 16 x 16 texels a cell and 4 texels between vertices, which keeps the tests fast.

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <components/puddle/puddlebake.hpp>

namespace PuddleTest
{
    inline Puddle::BakeSettings smallSettings()
    {
        Puddle::BakeSettings s;
        s.mCellSizeUnits = 1024;
        s.mCellVertices = 5;
        s.mSpacingUnits = 64;
        s.mMinPondTexels = 4;
        s.mHalo = 1;
        s.mCapacityCapSteps = 192;
        s.mCatchmentRadiusUnits = 1024;
        s.mWaterLevelUnits = -1;
        return s;
    }

    class FakeLand final : public Puddle::LandHeightSource
    {
    public:
        using HeightFn = std::function<float(int, int)>;
        using HasLandFn = std::function<bool(int, int)>;

        explicit FakeLand(HeightFn height, HasLandFn hasLand = nullptr, std::uint32_t vertices = 5)
            : mHeight(std::move(height))
            , mHasLand(std::move(hasLand))
            , mVertices(vertices)
        {
        }

        bool cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights) override
        {
            ++mReads;
            if (mHasLand && !mHasLand(cellX, cellY))
                return false;
            heights.resize(static_cast<std::size_t>(mVertices) * mVertices);
            const int intervals = static_cast<int>(mVertices) - 1;
            for (int j = 0; j < static_cast<int>(mVertices); ++j)
                for (int i = 0; i < static_cast<int>(mVertices); ++i)
                    heights[static_cast<std::size_t>(j) * mVertices + i]
                        = mHeight(cellX * intervals + i, cellY * intervals + j);
            return true;
        }

        std::size_t mReads = 0;

    private:
        HeightFn mHeight;
        HasLandFn mHasLand;
        std::uint32_t mVertices;
    };

    /// A flat plateau at `rim` with a round pit sunk to `rim - depth` at the vertex (centreX, centreY) (global
    /// vertices): the height is rim - depth * (1 - min(r^2 / 4, 1)) with r the distance to the centre in vertices.
    inline FakeLand::HeightFn pitHeights(int centreX, int centreY, float rim, float depth)
    {
        return [=](int vx, int vy) {
            const float dx = static_cast<float>(vx - centreX);
            const float dy = static_cast<float>(vy - centreY);
            const float r2 = (dx * dx + dy * dy) / 4.f;
            return rim - depth * (1.f - (r2 < 1.f ? r2 : 1.f));
        };
    }
}

#endif
