#include "puddlegpu.hpp"

#include <algorithm>

#include <components/render/puddlemapgpu.hpp>

namespace Puddle
{
    namespace PM = Render::PuddleMap;

    bool gpuTexelsFit(std::uint32_t texelsPerCell)
    {
        return texelsPerCell > 0 && texelsPerCell <= PM::maxTexelsPerSide;
    }

    void packOff(std::uint32_t* out, std::size_t words)
    {
        std::fill(out, out + std::min<std::size_t>(words, PM::dataBase), 0u);
    }

    GpuPackResult packWindow(
        std::uint32_t* out, std::size_t words, const GpuWindowParams& params, const CellFinder& find)
    {
        GpuPackResult result;
        std::fill(out, out + words, 0u);
        if (!gpuTexelsFit(params.mTexelsPerCell) || words < PM::bufferWords(PM::windowSide, params.mTexelsPerCell))
            return result;

        const std::uint32_t side = params.mTexelsPerCell;
        const std::size_t texels = static_cast<std::size_t>(side) * side;
        const std::int32_t originX = params.mCentreX - static_cast<std::int32_t>(PM::windowRadius);
        const std::int32_t originY = params.mCentreY - static_cast<std::int32_t>(PM::windowRadius);

        out[PM::wordEnabled] = 1;
        out[PM::wordDebug] = params.mDebugMode;
        out[PM::wordOriginX] = static_cast<std::uint32_t>(originX);
        out[PM::wordOriginY] = static_cast<std::uint32_t>(originY);
        out[PM::wordWindowSide] = PM::windowSide;
        out[PM::wordTexelsPerCell] = side;
        out[PM::wordSpacing] = params.mSpacingUnits;
        out[PM::wordCellSize] = params.mCellSizeUnits;
        out[PM::wordSupply] = PM::floatBits(params.mSupply);
        out[PM::wordAbsorption] = PM::floatBits(params.mAbsorption);
        out[PM::wordOptions] = params.mKeepStaticNoise ? PM::optionKeepStaticNoise : 0u;
        const std::uint32_t cellsInWindow = PM::windowSide * PM::windowSide;
        const bool withTops = words >= PM::bufferWordsWithTops(PM::windowSide, side);
        const std::uint32_t topsBase = PM::dataBase + cellsInWindow * static_cast<std::uint32_t>(texels);
        if (withTops)
            out[PM::wordTopsBase] = topsBase;

        for (std::uint32_t sy = 0; sy < PM::windowSide; ++sy)
        {
            for (std::uint32_t sx = 0; sx < PM::windowSide; ++sx)
            {
                const std::shared_ptr<const CellMap> cell
                    = find(originX + static_cast<std::int32_t>(sx), originY + static_cast<std::int32_t>(sy));
                if (!cell)
                    continue;
                const bool pits = !cell->noPits();
                if (cell->mSide != side || cell->mTexelFlags.size() != texels || cell->mTop.size() != texels
                    || (pits
                        && (cell->mCapacity.size() != texels || cell->mBasinMax.size() != texels
                            || cell->mCatchment.size() != texels)))
                {
                    ++result.mSkipped;
                    continue;
                }
                const std::uint32_t slot = sx + PM::windowSide * sy;
                const std::uint32_t start = PM::dataBase + slot * static_cast<std::uint32_t>(texels);
                out[PM::tableBase + slot] = start;
                if (withTops)
                {
                    out[topsBase + slot] = PM::floatBits(cell->mHeightBase);
                    const std::size_t topStart = topsBase + cellsInWindow + static_cast<std::size_t>(slot) * texels;
                    for (std::size_t i = 0; i < texels; ++i)
                        out[topStart + i] = cell->mTop[i];
                }
                for (std::size_t i = 0; i < texels; ++i)
                {
                    out[start + i] = pits ? PM::packTexel(cell->mCapacity[i], cell->mBasinMax[i], cell->mCatchment[i],
                                                cell->mTexelFlags[i])
                                          : PM::packTexel(0, 0, 0, cell->mTexelFlags[i]);
                }
                ++result.mPacked;
            }
        }
        return result;
    }
}
