// Stage C8 of Track V stage V6: land painted with a texture of the project being edited has plugin index -1, which
// mLayerIndices, a vector indexed by plugin, cannot hold. Such land showed the default layer until this.

#include <components/esm3terrain/storage.hpp>
#include <components/terrain/bounds.hpp>

#include <vsg/core/Array2D.h>
// MSVC needs Array2D<T>::accept, which these headers define, in this translation unit (as storage.cpp).
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Visitor.h>

#include <gtest/gtest.h>

namespace
{
    class ProjectStorage : public ESMTerrain::Storage
    {
    public:
        explicit ProjectStorage(const ESM::Land& land)
            : mLand(land)
        {
        }

        vsg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation location) override
        {
            if (location.mX != 0 || location.mY != 0)
                return {};
            return vsg::ref_ptr<const ESMTerrain::LandObject>(new ESMTerrain::LandObject(mLand, ESM::Land::DATA_VTEX));
        }

        std::map<PluginTexture, std::string> enumerateLayers() const override
        {
            return { { { 0, 0 }, "default.dds" }, { { 1, 0 }, "first.dds" }, { { 3, -1 }, "project.dds" } };
        }

    private:
        const ESM::Land& mLand;
    };

    TEST(ESMTerrainProjectLayers, landOfTheProjectTakesItsOwnLayer)
    {
        ESM::Land land;
        land.mX = 0;
        land.mY = 0;
        land.setPlugin(-1);
        land.mDataTypes = ESM::Land::DATA_VTEX;
        land.add(ESM::Land::DATA_VTEX);
        ESM::Land::LandData* data = land.getLandData();
        ASSERT_NE(data, nullptr);
        for (auto& texture : data->mTextures)
            texture = 3;

        ProjectStorage storage(land);
        const auto layers = storage.getLayers();
        ASSERT_EQ(layers.size(), 3u);
        // The map's order: the project's key sorts by texture index, after those of index 0 and 1.
        EXPECT_EQ(layers[2], "project.dds");

        const auto image = storage.getBlendmap(Terrain::Bounds{ 1.f, vsg::vec2(0, 0) });
        auto* blendmap = dynamic_cast<vsg::ubvec4Array2D*>(image.get());
        ASSERT_NE(blendmap, nullptr);
        // Away from the cell's edge, where the neighbouring land (none here) has a say.
        EXPECT_EQ((*blendmap)(8, 8)[0], 2);
    }
}
