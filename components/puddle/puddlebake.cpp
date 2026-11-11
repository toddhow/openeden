#include "puddlebake.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

#include <edenarchive/hash.hpp>

#include <components/render/puddlefill.hpp>
#include <components/render/puddlelevel.hpp>

namespace Puddle
{
    namespace
    {
        Error fail(const char* field, std::string message)
        {
            return Error{ field, 0, std::move(message) };
        }

        bool isPowerOfTwo(std::uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        void hashU32(EdenArchive::Hasher& hasher, std::uint32_t value)
        {
            const std::uint8_t bytes[4] = { static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
                static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24) };
            hasher.update(bytes, 4);
        }

        void hashFloats(EdenArchive::Hasher& hasher, const std::vector<float>& values)
        {
            for (const float value : values)
            {
                std::uint32_t bits;
                std::memcpy(&bits, &value, 4);
                hashU32(hasher, bits);
            }
        }

        // The cells of a window, in row-major order (south row first), each with its vertex heights if it has land.
        struct Window
        {
            std::uint32_t mCells = 0; // a side
            std::vector<std::vector<float>> mHeights;
            std::vector<std::uint8_t> mHasLand;
            // For each cell whose heights were refused: where and why (it then counts as no land).
            std::vector<std::string> mProblem;
            // Stage W-c (only with statics on): the digest of each land cell's references, and for a bake the raw
            // captured tops of each land cell.
            std::vector<Digest> mRefs;
            std::vector<std::uint8_t> mHasRefs;
            std::vector<std::vector<float>> mTops;
            std::string mStaticsProblem;
        };

        // The first height of a cell that the fill would refuse, as a located message; empty when all are fine.
        std::string badHeight(
            const std::vector<float>& heights, std::int32_t cellX, std::int32_t cellY, std::uint32_t vertices)
        {
            for (std::size_t i = 0; i < heights.size(); ++i)
            {
                if (std::isfinite(heights[i]) && std::abs(heights[i]) <= Render::Puddle::maxAbsHeight)
                    continue;
                return "cell (" + std::to_string(cellX) + ", " + std::to_string(cellY) + ") vertex ("
                    + std::to_string(i % vertices) + ", " + std::to_string(i / vertices) + "): the height "
                    + std::to_string(heights[i]) + " is not finite or is beyond +-"
                    + std::to_string(static_cast<long long>(Render::Puddle::maxAbsHeight));
            }
            return {};
        }

        Window fetchWindow(LandHeightSource& source, StaticSurfaceSource* statics, bool wantTops, std::int32_t cellX,
            std::int32_t cellY, const BakeSettings& s)
        {
            Window window;
            window.mCells = 2 * s.mHalo + 1;
            const std::size_t count = static_cast<std::size_t>(window.mCells) * window.mCells;
            window.mHeights.resize(count);
            window.mHasLand.assign(count, 0);
            window.mProblem.assign(count, std::string());
            const bool withStatics = s.mStatics && statics != nullptr;
            if (withStatics)
            {
                window.mRefs.assign(count, Digest{});
                window.mHasRefs.assign(count, 0);
                window.mTops.resize(count);
            }
            const std::size_t expected = static_cast<std::size_t>(s.mCellVertices) * s.mCellVertices;
            for (std::uint32_t iy = 0; iy < window.mCells; ++iy)
            {
                for (std::uint32_t ix = 0; ix < window.mCells; ++ix)
                {
                    const std::size_t index = static_cast<std::size_t>(iy) * window.mCells + ix;
                    const std::int32_t cx = cellX + static_cast<std::int32_t>(ix) - static_cast<std::int32_t>(s.mHalo);
                    const std::int32_t cy = cellY + static_cast<std::int32_t>(iy) - static_cast<std::int32_t>(s.mHalo);
                    std::vector<float> heights;
                    if (source.cellHeights(cx, cy, heights) && heights.size() == expected)
                    {
                        window.mProblem[index] = badHeight(heights, cx, cy, s.mCellVertices);
                        if (window.mProblem[index].empty())
                        {
                            window.mHeights[index] = std::move(heights);
                            window.mHasLand[index] = 1;
                            if (withStatics)
                            {
                                Digest refs{};
                                if (statics->refsDigest(cx, cy, refs))
                                {
                                    window.mRefs[index] = refs;
                                    window.mHasRefs[index] = 1;
                                }
                                if (wantTops)
                                {
                                    std::vector<float> tops;
                                    const std::size_t texels
                                        = static_cast<std::size_t>(s.texelsPerCell()) * s.texelsPerCell();
                                    if (statics->cellTops(cx, cy, tops) && tops.size() == texels)
                                        window.mTops[index] = std::move(tops);
                                    else if (window.mStaticsProblem.empty())
                                        window.mStaticsProblem = "no static surface capture for cell ("
                                            + std::to_string(cx) + ", " + std::to_string(cy) + ")";
                                }
                            }
                        }
                    }
                }
            }
            return window;
        }

        Digest digestOfWindow(const Window& window, std::int32_t cellX, std::int32_t cellY, const BakeSettings& s)
        {
            EdenArchive::Hasher hasher;
            hashU32(hasher, layoutVersion);
            hashU32(hasher, static_cast<std::uint32_t>(cellX));
            hashU32(hasher, static_cast<std::uint32_t>(cellY));
            hashU32(hasher, s.mHalo);
            hashU32(hasher, s.mCellVertices);
            hashU32(hasher, s.mCellSizeUnits);
            hashU32(hasher, static_cast<std::uint32_t>(s.mWaterLevelUnits));
            for (std::size_t i = 0; i < window.mHeights.size(); ++i)
            {
                const std::uint8_t land = window.mHasLand[i];
                hasher.update(&land, 1);
                if (land)
                    hashFloats(hasher, window.mHeights[i]);
            }
            if (s.mStatics)
            {
                // The references (and model contents) of every land cell; one byte says whether the digest could be
                // made, so a cell whose references cannot be read gives another digest, never the same as one that
                // can. Only with statics on: a statics-off digest is byte-for-byte that of stage W-b.
                for (std::size_t i = 0; i < window.mHeights.size(); ++i)
                {
                    if (!window.mHasLand[i])
                        continue;
                    const std::uint8_t has = window.mHasRefs.empty() ? 0 : window.mHasRefs[i];
                    hasher.update(&has, 1);
                    if (has)
                        hasher.update(window.mRefs[i].data(), window.mRefs[i].size());
                }
            }
            return hasher.finalize();
        }
    }

    std::uint32_t BakeSettings::texelsPerVertexStep() const
    {
        if (mCellVertices < 3 || mSpacingUnits == 0 || mCellSizeUnits % (mCellVertices - 1) != 0)
            return 0;
        const std::uint32_t step = mCellSizeUnits / (mCellVertices - 1);
        if (step % mSpacingUnits != 0)
            return 0;
        return step / mSpacingUnits;
    }

    std::uint32_t BakeSettings::texelsPerCell() const
    {
        const std::uint32_t perStep = texelsPerVertexStep();
        return perStep == 0 ? 0 : perStep * (mCellVertices - 1);
    }

    Error validateSettings(const BakeSettings& s)
    {
        if (s.mCellVertices < 3 || s.mCellSizeUnits == 0 || s.mCellSizeUnits % (s.mCellVertices - 1) != 0)
            return fail("cellSize", "the cell size must be a multiple of the vertex intervals of a cell");
        const std::uint32_t perStep = s.texelsPerVertexStep();
        if (s.mSpacingUnits == 0 || perStep == 0 || !isPowerOfTwo(perStep))
            return fail("spacing", "the spacing must divide the land's vertex spacing into a power of two texels");
        const std::uint32_t side = s.texelsPerCell();
        if (side < minSide || side > maxSide)
            return fail("spacing", "a cell must have from 8 to 1024 texels a side");
        if (s.mCapacityCapSteps < 1 || s.mCapacityCapSteps > 255)
            return fail("capacityCap", "the maximum puddle depth must be from 1/16 to 255/16 units");
        if (s.mMinPondTexels < 1)
            return fail("minPondTexels", "the minimum pond area must be at least one texel");
        if (s.mHalo > 4)
            return fail("halo", "the halo must be at most 4 cells");
        const std::uint64_t windowSide = static_cast<std::uint64_t>(2 * s.mHalo + 1) * side;
        if (windowSide > static_cast<std::uint64_t>(Render::Puddle::maxGridSide)
            || windowSide * windowSide > Render::Puddle::maxGridTexels)
            return fail("halo", "the window is larger than the fill accepts");
        if (s.mCatchmentRadiusUnits > 1000000)
            return fail("catchmentRadius", "the catchment radius must be at most 1,000,000 units");
        return {};
    }

    float terrainHeightAt(const std::vector<float>& heights, std::uint32_t vertices, std::uint32_t texelsPerStep,
        std::uint32_t tx, std::uint32_t ty)
    {
        const std::uint32_t i = tx / texelsPerStep;
        const std::uint32_t j = ty / texelsPerStep;
        const std::uint32_t a = tx % texelsPerStep;
        const std::uint32_t b = ty % texelsPerStep;
        auto at = [&](std::uint32_t x, std::uint32_t y) { return static_cast<double>(heights[y * vertices + x]); };
        if (a == 0 && b == 0)
            return heights[j * vertices + i];
        const double fa = static_cast<double>(a) / texelsPerStep;
        const double fb = static_cast<double>(b) / texelsPerStep;
        const double h00 = at(i, j);
        const double h10 = at(i + 1, j);
        const double h01 = at(i, j + 1);
        const double h11 = at(i + 1, j + 1);
        double h;
        if (((i + j) & 1u) != 0)
        {
            // Diagonal from (i, j + 1) to (i + 1, j).
            if (fa + fb <= 1.0)
                h = h00 + fa * (h10 - h00) + fb * (h01 - h00);
            else
                h = h11 + (1.0 - fa) * (h01 - h11) + (1.0 - fb) * (h10 - h11);
        }
        else
        {
            // Diagonal from (i, j) to (i + 1, j + 1).
            if (fa >= fb)
                h = h00 + fa * (h10 - h00) + fb * (h11 - h10);
            else
                h = h00 + fb * (h01 - h00) + fa * (h11 - h01);
        }
        return static_cast<float>(h);
    }

    float heightBaseOf(float lowest)
    {
        return std::isfinite(lowest) ? std::floor(lowest) : 0.f;
    }

    Digest parameterDigest(const BakeSettings& s)
    {
        EdenArchive::Hasher hasher;
        hashU32(hasher, bakerVersion);
        hashU32(hasher, layoutVersion);
        hashU32(hasher, s.mSpacingUnits);
        hashU32(hasher, s.mCapacityCapSteps);
        hashU32(hasher, s.mMinPondTexels);
        hashU32(hasher, s.mCatchmentRadiusUnits);
        hashU32(hasher, s.mHalo);
        hashU32(hasher, s.mStatics ? 1u : 0u);
        hashU32(hasher, static_cast<std::uint32_t>(s.mWaterLevelUnits));
        hashU32(hasher, s.mCellSizeUnits);
        hashU32(hasher, s.mCellVertices);
        // Stage W-c: what the static filter does is a parameter of a bake that uses statics (and of no other).
        if (s.mStatics)
            hashU32(hasher, staticFilterVersion);
        return hasher.finalize();
    }

    Digest inputDigest(LandHeightSource& source, std::int32_t cellX, std::int32_t cellY, const BakeSettings& settings,
        StaticSurfaceSource* statics)
    {
        return digestOfWindow(fetchWindow(source, statics, false, cellX, cellY, settings), cellX, cellY, settings);
    }

    BakeResult bakeCell(LandHeightSource& source, std::int32_t cellX, std::int32_t cellY, const BakeSettings& s,
        const BakeOptions& options, StaticSurfaceSource* statics)
    {
        namespace RP = Render::Puddle;
        BakeResult result;
        if (Error error = validateSettings(s))
        {
            result.mError = std::move(error);
            return result;
        }
        const std::uint32_t side = s.texelsPerCell();
        const std::uint32_t perStep = s.texelsPerVertexStep();
        if (s.mStatics && statics == nullptr)
        {
            result.mError = fail("statics", "the settings ask for static surfaces and there is no source for them");
            return result;
        }
        const Window window = fetchWindow(source, statics, true, cellX, cellY, s);
        {
            const std::size_t centre = static_cast<std::size_t>(s.mHalo) * window.mCells + s.mHalo;
            if (!window.mProblem[centre].empty())
            {
                result.mError = fail("heights", window.mProblem[centre]);
                return result;
            }
        }
        if (s.mStatics)
        {
            // Every land cell of the window needs its references' digest and its capture, or the answer would depend
            // on what happened to be captured.
            if (!window.mStaticsProblem.empty())
            {
                result.mError = fail("statics", window.mStaticsProblem);
                return result;
            }
            for (std::size_t i = 0; i < window.mHasLand.size(); ++i)
            {
                if (window.mHasLand[i] && !window.mHasRefs[i])
                {
                    result.mError = fail("statics", "the references of a cell of the window cannot be read");
                    return result;
                }
            }
        }
        const std::uint32_t windowSide = window.mCells * side;
        const std::size_t count = static_cast<std::size_t>(windowSide) * windowSide;
        result.mStats.mWindowTexels = static_cast<std::uint32_t>(count);

        // 1. The ground.
        const float water = static_cast<float>(s.mWaterLevelUnits);
        std::vector<float> heights(count, water);
        std::vector<std::uint8_t> sinks(count, 1);
        for (std::uint32_t iy = 0; iy < window.mCells; ++iy)
        {
            for (std::uint32_t ix = 0; ix < window.mCells; ++ix)
            {
                const std::size_t cellIndex = static_cast<std::size_t>(iy) * window.mCells + ix;
                if (!window.mHasLand[cellIndex])
                    continue;
                for (std::uint32_t ty = 0; ty < side; ++ty)
                {
                    for (std::uint32_t tx = 0; tx < side; ++tx)
                    {
                        const float h = terrainHeightAt(window.mHeights[cellIndex], s.mCellVertices, perStep, tx, ty);
                        const std::size_t index
                            = static_cast<std::size_t>(iy * side + ty) * windowSide + (ix * side + tx);
                        heights[index] = h;
                        sinks[index] = h <= water ? 1 : 0;
                    }
                }
            }
        }

        // 1b. Stage W-c: the top-most static surface of each column joins the height field (research note 0049 4.3):
        // H = max(terrain, accepted static top); a surface above the water is not a sink.
        std::vector<std::uint8_t> staticTexel(count, 0);
        if (s.mStatics)
        {
            std::vector<float> raw(count, std::numeric_limits<float>::quiet_NaN());
            for (std::uint32_t iy = 0; iy < window.mCells; ++iy)
            {
                for (std::uint32_t ix = 0; ix < window.mCells; ++ix)
                {
                    const std::size_t cellIndex = static_cast<std::size_t>(iy) * window.mCells + ix;
                    if (!window.mHasLand[cellIndex])
                        continue;
                    for (std::uint32_t ty = 0; ty < side; ++ty)
                        for (std::uint32_t tx = 0; tx < side; ++tx)
                            raw[static_cast<std::size_t>(iy * side + ty) * windowSide + (ix * side + tx)]
                                = window.mTops[cellIndex][static_cast<std::size_t>(ty) * side + tx];
                }
            }
            const StaticFilterResult filtered
                = applyStatics(heights, raw, windowSide, static_cast<float>(s.mSpacingUnits));
            result.mStats.mStaticCandidates = filtered.mCandidates;
            for (std::size_t t = 0; t < count; ++t)
            {
                if (!filtered.mIsStatic[t])
                    continue;
                staticTexel[t] = 1;
                heights[t] = filtered.mHeights[t];
                sinks[t] = heights[t] <= water ? 1 : 0;
            }
        }

        // 2. Fill with no cap, label the pits, measure from the floor.
        const RP::FillResult fill = RP::fillDepressions(heights, sinks, static_cast<int>(windowSide),
            static_cast<int>(windowSide), std::numeric_limits<float>::infinity());
        if (!fill.ok())
        {
            result.mError = fail("fill", fill.error);
            return result;
        }
        const RP::Basins basins = RP::labelBasins(fill, heights);
        const RP::FloorCapacity floor = RP::capacityFromFloor(fill, basins, s.capacityCap());
        if (!floor.ok())
        {
            result.mError = fail("floor", floor.error);
            return result;
        }

        // 3 and 4. The pond: texels whose stored capacity is above zero; small ponds are dropped.
        std::vector<float> pond(count, 0.f);
        std::vector<std::uint32_t> footprint(basins.basins.size(), 0);
        for (std::size_t t = 0; t < count; ++t)
        {
            const std::int32_t id = basins.id[t];
            if (id < 0 || RP::quantiseCapacity(floor.capacity[t]) == 0)
                continue;
            pond[t] = floor.capacity[t];
            ++footprint[static_cast<std::size_t>(id)];
        }
        std::vector<std::uint8_t> kept(basins.basins.size(), 0);
        for (std::size_t b = 0; b < kept.size(); ++b)
        {
            if (footprint[b] == 0)
                continue;
            ++result.mStats.mPitsFound;
            if (footprint[b] >= s.mMinPondTexels)
            {
                kept[b] = 1;
                ++result.mStats.mPitsKept;
            }
            else
                ++result.mStats.mPitsDroppedByArea;
        }
        for (std::size_t t = 0; t < count; ++t)
        {
            if (pond[t] > 0.f && !kept[static_cast<std::size_t>(basins.id[t])])
                pond[t] = 0.f;
        }

        // 5. The catchment, bounded; the unbounded one is measured for the report.
        const std::int64_t radiusTenths
            = static_cast<std::int64_t>(s.mCatchmentRadiusUnits) * 10 / static_cast<std::int64_t>(s.mSpacingUnits);
        const RP::Catchment bounded = RP::catchmentRatioBounded(basins, fill, pond, radiusTenths);
        RP::Catchment unbounded;
        if (options.mStatistics)
            unbounded = RP::catchmentRatio(basins, fill, RP::accumulateFlow(fill), pond);
        if (!bounded.ok() || !unbounded.ok())
        {
            result.mError = fail("catchment", bounded.ok() ? unbounded.error : bounded.error);
            return result;
        }
        std::vector<float> basinMax(basins.basins.size(), 0.f);
        for (std::size_t t = 0; t < count; ++t)
        {
            if (pond[t] > 0.f)
            {
                float& m = basinMax[static_cast<std::size_t>(basins.id[t])];
                m = std::max(m, pond[t]);
            }
        }

        // 6. The cell's own texels.
        CellMap cell;
        cell.mCellX = cellX;
        cell.mCellY = cellY;
        cell.mSide = side;
        cell.mSpacing = static_cast<float>(s.mSpacingUnits);
        cell.mFlags = s.mStatics ? flagStatics : 0;
        cell.mParameterDigest = parameterDigest(s);
        cell.mInputDigest = digestOfWindow(window, cellX, cellY, s);
        const std::size_t n = static_cast<std::size_t>(side) * side;
        const std::uint32_t origin = s.mHalo * side;
        float lowest = std::numeric_limits<float>::infinity();
        for (std::uint32_t ty = 0; ty < side; ++ty)
            for (std::uint32_t tx = 0; tx < side; ++tx)
                lowest = std::min(lowest, heights[static_cast<std::size_t>(origin + ty) * windowSide + origin + tx]);
        cell.mHeightBase = heightBaseOf(lowest);
        cell.mCapacity.assign(n, 0);
        cell.mBasinMax.assign(n, 0);
        cell.mCatchment.assign(n, 0);
        cell.mTexelFlags.assign(n, 0);
        cell.mTop.assign(n, 0);
        cell.mBasinId.assign(n, noBasin);
        std::map<std::int32_t, std::uint16_t> local; // window pit id -> id in the cell, in order of first use
        std::vector<std::int32_t> localToWindow;
        for (std::uint32_t ty = 0; ty < side; ++ty)
        {
            for (std::uint32_t tx = 0; tx < side; ++tx)
            {
                const std::size_t out = static_cast<std::size_t>(ty) * side + tx;
                const std::size_t in = static_cast<std::size_t>(origin + ty) * windowSide + origin + tx;
                const bool clamped = !RP::heightRepresentable(heights[in], cell.mHeightBase);
                if (clamped)
                    result.mStats.mHeightsClamped = true;
                cell.mTop[out] = RP::quantiseHeight(heights[in], cell.mHeightBase);
                const std::uint8_t direction = RP::flowDirection(fill, in);
                cell.mTexelFlags[out] = direction == RP::flowNone ? texelSink : (direction & texelDirectionMask);
                if (staticTexel[in])
                {
                    cell.mTexelFlags[out] |= texelStatic;
                    ++result.mStats.mStaticTexels;
                }
                const std::uint8_t capacityCode = RP::quantiseCapacity(pond[in]);
                if (capacityCode == 0)
                    continue;
                if (clamped)
                {
                    // The height is saturated, so a level test against it would be wrong: no pond here.
                    ++result.mStats.mPondTexelsDroppedClamped;
                    continue;
                }
                const std::int32_t windowId = basins.id[in];
                auto found = local.find(windowId);
                if (found == local.end())
                {
                    found = local.emplace(windowId, static_cast<std::uint16_t>(localToWindow.size())).first;
                    localToWindow.push_back(windowId);
                }
                cell.mBasinId[out] = found->second;
                cell.mCapacity[out] = capacityCode;
                cell.mBasinMax[out] = RP::quantiseCapacity(basinMax[static_cast<std::size_t>(windowId)]);
                cell.mCatchment[out] = RP::quantiseKappa(bounded.ratio[static_cast<std::size_t>(windowId)]);
                ++result.mStats.mWetTexels;
            }
        }
        if (localToWindow.empty())
        {
            cell.mFlags |= flagNoPits;
            cell.mCapacity.clear();
            cell.mBasinMax.clear();
            cell.mCatchment.clear();
            cell.mBasinId.clear();
        }
        else
        {
            const float clipAbove = std::exp2(RP::maxLogKappa);
            for (const std::int32_t windowId : localToWindow)
            {
                const std::size_t w = static_cast<std::size_t>(windowId);
                BasinInfo info;
                info.mArea = footprint[w];
                info.mMaxCapacityCode = RP::quantiseCapacity(basinMax[w]);
                info.mCatchmentCode = RP::quantiseKappa(bounded.ratio[w]);
                cell.mBasins.push_back(info);
                result.mStats.mKappasBounded.push_back(bounded.ratio[w]);
                if (options.mStatistics)
                {
                    result.mStats.mKappasUnbounded.push_back(unbounded.ratio[w]);
                    if (unbounded.ratio[w] > clipAbove)
                        ++result.mStats.mClippedUnbounded;
                }
                if (bounded.ratio[w] > clipAbove)
                    ++result.mStats.mClippedBounded;
            }
        }
        result.mStats.mPitsInCell = static_cast<std::uint32_t>(localToWindow.size());
        result.mCell = std::move(cell);
        return result;
    }
}
