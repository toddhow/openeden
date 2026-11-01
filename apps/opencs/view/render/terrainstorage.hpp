#ifndef OPENCS_RENDER_TERRAINSTORAGE_H
#define OPENCS_RENDER_TERRAINSTORAGE_H

#include <array>
#include <map>
#include <span>
#include <string>

#include <vsg/maths/vec4.h>

#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/esm3terrain/storage.hpp>

namespace CSMWorld
{
    class Data;
}

namespace CSVRender
{
    /**
     * @brief A bridge between the terrain component and Eden Studio's terrain data storage.
     *
     * The alteration hooks have no cell argument, so one storage serves one cell: ask it only for that cell's bounds
     * (Cell, through Terrain::Paging::updateViewForCell with a batch of one cell).
     */
    class TerrainStorage : public ESMTerrain::Storage
    {
    public:
        explicit TerrainStorage(const CSMWorld::Data& data);
        void setAlteredHeight(int inCellX, int inCellY, float heightMap);
        void resetHeights();

        float getSumOfAlteredAndTrueHeight(int cellX, int cellY, int inCellX, int inCellY);
        float* getAlteredHeight(int inCellX, int inCellY);

        vsg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellLocation) override;
        std::map<PluginTexture, std::string> enumerateLayers() const override;

    private:
        const CSMWorld::Data& mData;
        std::array<float, ESM::Land::LAND_SIZE * ESM::Land::LAND_SIZE> mAlteredHeight;

        bool useAlteration() const override { return true; }

        int getThisHeight(int col, int row, std::span<const float> heightData) const;
        int getLeftHeight(int col, int row, std::span<const float> heightData) const;
        int getRightHeight(int col, int row, std::span<const float> heightData) const;
        int getUpHeight(int col, int row, std::span<const float> heightData) const;
        int getDownHeight(int col, int row, std::span<const float> heightData) const;
        int getHeightDifferenceToLeft(int col, int row, std::span<const float> heightData) const;
        int getHeightDifferenceToRight(int col, int row, std::span<const float> heightData) const;
        int getHeightDifferenceToUp(int col, int row, std::span<const float> heightData) const;
        int getHeightDifferenceToDown(int col, int row, std::span<const float> heightData) const;
        bool leftOrUpIsOverTheLimit(int col, int row, int heightWarningLimit, std::span<const float> heightData) const;
        bool rightOrDownIsOverTheLimit(
            int col, int row, int heightWarningLimit, std::span<const float> heightData) const;

        void adjustColor(int col, int row, const ESM::Land::LandData* heightData, vsg::ubvec4& color) const override;
        float getAlteredHeight(int col, int row) const override;
    };

}

#endif
