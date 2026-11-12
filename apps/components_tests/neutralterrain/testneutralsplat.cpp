// Task 0149: the alpha layers of a quadrant as a splat blend map (components/neutralterrain/splat.*) and the storage
// that builds it. Synthetic data, no game files.
#include <components/neutralterrain/cellcache.hpp>
#include <components/neutralterrain/splat.hpp>
#include <components/neutralterrain/storage.hpp>

#include <components/terrain/bounds.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

namespace
{
    namespace sdk = openeden::sdk::game;
    using namespace NeutralTerrain;
    using namespace NeutralTerrain::Splat;
    using NeutralTerrain::CacheLimits;
    using NeutralTerrain::validateTerrainCell;

    std::vector<std::uint8_t> opacityAt(std::initializer_list<std::pair<int, int>> entries)
    {
        std::vector<std::uint8_t> v(kVertices, 0);
        for (const auto& e : entries)
            v[static_cast<std::size_t>(e.first)] = static_cast<std::uint8_t>(e.second);
        return v;
    }
    int sum(const std::array<std::uint8_t, kSlots>& w)
    {
        return std::accumulate(w.begin(), w.end(), 0);
    }

    // ---- composeQuadrant ------------------------------------------------------------------------------------------

    TEST(NeutralSplatCompose, noLayersIsTheBaseEverywhere)
    {
        const Quadrant q = composeQuadrant(7, {});
        for (std::uint8_t id : q.mIds)
            EXPECT_EQ(id, 7);
        ASSERT_EQ(q.mWeights.size(), static_cast<std::size_t>(kVertices));
        for (const auto& w : q.mWeights)
        {
            EXPECT_EQ(w[0], 255);
            EXPECT_EQ(sum(w), 255);
        }
    }

    TEST(NeutralSplatCompose, aFullyOpaqueVertexShowsOnlyTheLayer)
    {
        const auto opacity = opacityAt({ { 5, 255 }, { 6, 128 } });
        const LayerIn layers[] = { { 9, &opacity } };
        const Quadrant q = composeQuadrant(3, layers);
        EXPECT_EQ(q.mIds[0], 3);
        EXPECT_EQ(q.mIds[1], 9);
        EXPECT_EQ(q.mIds[2], 3); // padded with the base
        EXPECT_EQ(q.mIds[3], 3);
        EXPECT_EQ(q.mWeights[0][0], 255);
        EXPECT_EQ(q.mWeights[0][1], 0);
        EXPECT_EQ(q.mWeights[5][0], 0);
        EXPECT_EQ(q.mWeights[5][1], 255);
        // half opaque: the layer 128/255 and the base the rest
        EXPECT_EQ(q.mWeights[6][1], 128);
        EXPECT_EQ(q.mWeights[6][0], 127);
    }

    TEST(NeutralSplatCompose, laterLayersAreDrawnOverEarlierOnes)
    {
        const auto a = opacityAt({ { 10, 255 }, { 11, 128 } });
        const auto b = opacityAt({ { 10, 255 }, { 11, 128 } });
        const LayerIn layers[] = { { 1, &a }, { 2, &b } };
        const Quadrant q = composeQuadrant(0, layers);
        // fully opaque on top hides everything below
        EXPECT_EQ(q.mWeights[10][2], 255);
        EXPECT_EQ(q.mWeights[10][1], 0);
        EXPECT_EQ(q.mWeights[10][0], 0);
        // two half layers: top 0.502, middle 0.502 * 0.498 = 0.250, base 0.248
        EXPECT_NEAR(q.mWeights[11][2], 128, 1);
        EXPECT_NEAR(q.mWeights[11][1], 64, 1);
        EXPECT_NEAR(q.mWeights[11][0], 63, 1);
        EXPECT_EQ(sum(q.mWeights[11]), 255);
    }

    TEST(NeutralSplatCompose, everyVertexSumsTo255)
    {
        std::vector<std::uint8_t> a(kVertices), b(kVertices), c(kVertices);
        for (int i = 0; i < kVertices; ++i)
        {
            a[i] = static_cast<std::uint8_t>((i * 37) % 256);
            b[i] = static_cast<std::uint8_t>((i * 91 + 13) % 256);
            c[i] = static_cast<std::uint8_t>((i * 5) % 256);
        }
        const LayerIn layers[] = { { 1, &a }, { 2, &b }, { 3, &c } };
        const Quadrant q = composeQuadrant(0, layers);
        for (const auto& w : q.mWeights)
            ASSERT_EQ(sum(w), 255);
    }

    TEST(NeutralSplatCompose, moreThanFourLayersKeepTheStrongestAndStillSum)
    {
        const auto weak = opacityAt({ { 20, 10 } }); // never above 4 percent
        const auto s1 = opacityAt({ { 20, 255 }, { 21, 200 } });
        const auto s2 = opacityAt({ { 22, 255 } });
        const auto s3 = opacityAt({ { 23, 255 } });
        const LayerIn layers[] = { { 1, &weak }, { 2, &s1 }, { 3, &s2 }, { 4, &s3 } }; // base + 4 = 5 layers
        const Quadrant q = composeQuadrant(0, layers);
        for (std::uint8_t id : q.mIds)
            EXPECT_NE(id, 1) << "the weakest layer should be dropped";
        for (const auto& w : q.mWeights)
            ASSERT_EQ(sum(w), 255);
        // The strong layers are untouched where they are alone on top.
        EXPECT_EQ(q.mWeights[22][2], 255);
        EXPECT_EQ(q.mWeights[23][3], 255);
    }

    TEST(NeutralSplatCompose, aMissingOrShortOpacityAddsNothing)
    {
        const std::vector<std::uint8_t> shortOne(10, 255);
        const LayerIn layers[] = { { 4, nullptr }, { 5, &shortOne } };
        const Quadrant q = composeQuadrant(2, layers);
        for (const auto& w : q.mWeights)
            EXPECT_EQ(w[0], 255);
    }

    // ---- layout and lookup ----------------------------------------------------------------------------------------

    TEST(NeutralSplatLayout, aCellIsTwoByTwoSlots)
    {
        const Layout l = layoutFor(1.f);
        EXPECT_EQ(l.mK, 2);
        EXPECT_EQ(l.mS, 17);
        EXPECT_EQ(l.mWidth, 34);
        EXPECT_EQ(l.mHeight, 36);
        EXPECT_FLOAT_EQ(tilesAcross(l), 16.f);
    }

    TEST(NeutralSplatLayout, aChunkSmallerThanAQuadrantIsOneSlotOfItsOwnVertices)
    {
        const Layout l = layoutFor(0.125f);
        EXPECT_EQ(l.mK, 1);
        EXPECT_EQ(l.mS, 5);
        EXPECT_EQ(l.mWidth, 5);
        EXPECT_EQ(l.mHeight, 6);
        EXPECT_FLOAT_EQ(tilesAcross(l), 2.f);
        const Layout half = layoutFor(0.5f);
        EXPECT_EQ(half.mK, 1);
        EXPECT_EQ(half.mS, 17);
        EXPECT_FLOAT_EQ(tilesAcross(half), 8.f);
    }

    TEST(NeutralSplatLayout, aBigChunkHasSeventeenTexelsPerQuadrant)
    {
        const Layout l = layoutFor(4.f);
        EXPECT_EQ(l.mK, 8);
        EXPECT_EQ(l.mS, 17);
        EXPECT_EQ(l.mWidth, 136);
        EXPECT_EQ(l.mHeight, 144);
        EXPECT_FLOAT_EQ(tilesAcross(l), 64.f);
    }

    TEST(NeutralSplatLayout, theShaderCanDeriveTheLayoutFromTheSizeAlone)
    {
        for (float size : { 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 16.f })
        {
            const Layout a = layoutFor(size);
            ASSERT_TRUE(isSplatSize(a.mWidth, a.mHeight)) << size;
            const Layout b = layoutOfSize(a.mWidth, a.mHeight);
            EXPECT_EQ(b.mK, a.mK) << size;
            EXPECT_EQ(b.mS, a.mS) << size;
        }
        EXPECT_FALSE(isSplatSize(16, 16));
    }

    TEST(NeutralSplatLayout, lookupMapsTheChunkOntoWholeSlots)
    {
        const Layout l = layoutFor(1.f); // 2 x 2 slots of 17 texels
        // The south-west corner of the chunk is the centre of texel (0, 0) of slot (0, 0).
        Lookup a = lookupFor(l, 0.f, 0.f);
        EXPECT_EQ(a.mSlotX, 0);
        EXPECT_EQ(a.mSlotY, 0);
        EXPECT_NEAR(a.mVertexX, 0.f, 1e-6);
        EXPECT_NEAR(a.mVertexY, 0.f, 1e-6);
        // The middle of the chunk is the first vertex of the north-east slot, not the last of the south-west one.
        Lookup b = lookupFor(l, 0.5f, 0.5f);
        EXPECT_EQ(b.mSlotX, 1);
        EXPECT_EQ(b.mSlotY, 1);
        EXPECT_NEAR(b.mVertexX, 0.f, 1e-6);
        EXPECT_NEAR(b.mVertexY, 0.f, 1e-6);
        // Just below the middle stays in the first slot, at its last vertex: the filter never mixes two slots.
        Lookup c = lookupFor(l, 0.4999999f, 0.f);
        EXPECT_EQ(c.mSlotX, 0);
        EXPECT_NEAR(c.mVertexX, 16.f, 1e-3);
        // The far edge of the chunk is the last texel of the last slot.
        Lookup d = lookupFor(l, 1.f, 1.f);
        EXPECT_EQ(d.mSlotX, 1);
        EXPECT_EQ(d.mSlotY, 1);
        EXPECT_NEAR(d.mVertexX, 16.f, 1e-6);
        EXPECT_NEAR(d.mVertexY, 16.f, 1e-6);
        // An eighth of the way along the chunk is 4 intervals into the first slot.
        Lookup e = lookupFor(l, 0.125f, 0.f);
        EXPECT_EQ(e.mSlotX, 0);
        EXPECT_NEAR(e.mVertexX, 4.f, 1e-5);
    }

    // ---- the storage ----------------------------------------------------------------------------------------------

    constexpr sdk::WorldId kRoot = 10;
    constexpr sdk::WorldId kGrass = 100, kRock = 101, kSand = 102;

    class FakeSource final : public sdk::WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fake"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<sdk::WorldId>&) const override { return 0; }
        bool buildCell(sdk::WorldId, sdk::WorldCell&, std::string& e) override
        {
            e = "none";
            return false;
        }
        bool worldInfo(sdk::WorldId id, sdk::WorldSpaceInfo& out) const override
        {
            if (id != kRoot)
                return false;
            out = sdk::WorldSpaceInfo();
            out.id = kRoot;
            out.editorId = "Root";
            return true;
        }
        sdk::TerrainResult buildTerrain(
            sdk::WorldId world, std::int32_t x, std::int32_t y, sdk::TerrainCell& out, std::string&) override
        {
            const auto found = mLands.find({ world, x, y });
            if (found == mLands.end())
                return sdk::TerrainResult::NoLand;
            out = found->second;
            return sdk::TerrainResult::Built;
        }
        std::size_t listGroundTextures(std::vector<sdk::GroundTexture>& out) const override
        {
            for (const auto& [id, path] : { std::pair<sdk::WorldId, const char*>{ 0, "textures\\landscape\\dirt02.dds" },
                     { kGrass, "textures\\landscape\\grass.dds" }, { kRock, "textures\\landscape\\rock.dds" },
                     { kSand, "textures\\landscape\\sand.dds" } })
            {
                sdk::GroundTexture t;
                t.id = id;
                t.diffusePath = path;
                out.push_back(t);
            }
            return 4;
        }
        static sdk::TerrainCell cell(std::int32_t x, std::int32_t y)
        {
            sdk::TerrainCell c;
            c.world = kRoot;
            c.gridX = x;
            c.gridY = y;
            c.vertsPerSide = 33;
            c.hasHeights = true;
            c.heights.assign(33 * 33, 0.f);
            c.baseTextures = { kGrass, kRock, kGrass, kSand };
            return c;
        }
        std::map<std::tuple<sdk::WorldId, std::int32_t, std::int32_t>, sdk::TerrainCell> mLands;
    };

    struct Rig
    {
        FakeSource mSource;
        std::shared_ptr<CellCache> mCache;
        std::unique_ptr<Storage> mStorage;
        void start(bool splat = true)
        {
            mCache = std::make_shared<CellCache>(mSource);
            std::string error;
            ASSERT_TRUE(mCache->setWorld(kRoot, error)) << error;
            mStorage = std::make_unique<Storage>(mCache);
            mStorage->setLayerSplat(splat);
        }
        void ensure(int x0, int y0, int x1, int y1) { mCache->ensure(x0, y0, x1, y1); }
    };

    Terrain::Bounds bounds(float x, float y, float size)
    {
        Terrain::Bounds b;
        b.min = vsg::vec2(x, y);
        b.size = size;
        return b;
    }

    const vsg::ubvec4Array2D* asImage(const vsg::ref_ptr<vsg::Data>& data)
    {
        return dynamic_cast<const vsg::ubvec4Array2D*>(data.get());
    }

    sdk::TerrainLayer layer(sdk::WorldId texture, std::initializer_list<std::pair<int, int>> entries)
    {
        sdk::TerrainLayer l;
        l.texture = texture;
        l.opacity = opacityAt(entries);
        return l;
    }

    TEST(NeutralSplatStorage, withTheSwitchOffTheBlendMapIsTheOldSquareOne)
    {
        Rig rig;
        rig.mSource.mLands[{ kRoot, 0, 0 }] = FakeSource::cell(0, 0);
        rig.start(false);
        rig.ensure(0, 0, 0, 0);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 16u);
        EXPECT_EQ(image->height(), 16u);
    }

    TEST(NeutralSplatStorage, aCellsBlendMapHasWeightsAndQuadrantIds)
    {
        Rig rig;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        c.layers[0].push_back(layer(kRock, { { 4 * 17 + 3, 255 } })); // row 4, column 3 of quadrant 0
        c.layers[3].push_back(layer(kSand, { { 0, 128 } }));
        rig.mSource.mLands[{ kRoot, 0, 0 }] = c;
        rig.start();
        rig.ensure(0, 0, 1, 1);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(image->width(), 34u);
        ASSERT_EQ(image->height(), 36u);
        // texture array layers: 0 default, 1 grass, 2 rock, 3 sand
        // quadrant 0 (south-west): ids at (0, 34): base grass, rock, then padded with the base
        EXPECT_EQ((*image)(0, 34)[0], 1);
        EXPECT_EQ((*image)(0, 34)[1], 2);
        EXPECT_EQ((*image)(0, 34)[2], 1);
        // its weights at column 3, row 4
        EXPECT_EQ((*image)(3, 4)[0], 0);
        EXPECT_EQ((*image)(3, 4)[1], 255);
        EXPECT_EQ((*image)(2, 4)[0], 255);
        // quadrant 1 (south-east) has its own slot: ids at (1, 34), weights from column 17
        EXPECT_EQ((*image)(1, 34)[0], 2);
        EXPECT_EQ((*image)(17, 0)[0], 255);
        // quadrant 3 (north-east): ids at (1, 35), the first vertex of its slot is at (17, 17)
        EXPECT_EQ((*image)(1, 35)[0], 3);
        EXPECT_EQ((*image)(1, 35)[1], 3);
        EXPECT_EQ((*image)(17, 17)[0], 127);
        EXPECT_EQ((*image)(17, 17)[1], 128);
        // quadrant 2 (north-west): base grass
        EXPECT_EQ((*image)(0, 35)[0], 1);
    }

    TEST(NeutralSplatStorage, aCellWithNoLandIsTheDefaultTexture)
    {
        Rig rig;
        rig.start();
        rig.ensure(0, 0, 0, 0);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ((*image)(5, 5)[0], 255);
        EXPECT_EQ((*image)(5, 5)[1], 0);
        EXPECT_EQ((*image)(0, 34)[0], 0);
    }

    TEST(NeutralSplatStorage, aBigChunkTakesEachCellsOwnQuadrants)
    {
        Rig rig;
        sdk::TerrainCell a = FakeSource::cell(0, 0);
        sdk::TerrainCell b = FakeSource::cell(1, 0);
        b.baseTextures = { kSand, kSand, kSand, kSand };
        rig.mSource.mLands[{ kRoot, 0, 0 }] = a;
        rig.mSource.mLands[{ kRoot, 1, 0 }] = b;
        rig.start();
        rig.ensure(0, 0, 1, 1);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 2.f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(image->width(), 68u); // 4 x 4 slots
        ASSERT_EQ(image->height(), 72u);
        EXPECT_EQ((*image)(0, 68)[0], 1); // cell (0,0) quadrant 0: grass
        EXPECT_EQ((*image)(1, 68)[0], 2); // quadrant 1: rock
        EXPECT_EQ((*image)(2, 68)[0], 3); // cell (1, 0) quadrant 0: sand
        EXPECT_EQ((*image)(3, 68)[0], 3);
        EXPECT_EQ((*image)(0, 69)[0], 1); // cell (0,0) quadrant 2: grass
        EXPECT_EQ((*image)(0, 70)[0], 0); // cell (0, 1) has no land: default
    }

    TEST(NeutralSplatStorage, aChunkInsideAQuadrantTakesOnlyItsOwnVertices)
    {
        Rig rig;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        // quadrant 2 (north-west) vertex column 8, row 0 is the south-west corner of the chunk below
        c.layers[2].push_back(layer(kRock, { { 8, 255 }, { 9, 128 }, { 4 * 17 + 12, 255 } }));
        rig.mSource.mLands[{ kRoot, 0, 0 }] = c;
        rig.start();
        rig.ensure(0, 0, 0, 0);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.25f, 0.5f, 0.125f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(image->width(), 5u);
        ASSERT_EQ(image->height(), 6u);
        EXPECT_EQ((*image)(0, 0)[1], 255); // vertex (8, 0)
        EXPECT_EQ((*image)(1, 0)[1], 128); // vertex (9, 0)
        EXPECT_EQ((*image)(4, 4)[1], 255); // vertex (12, 4)
        EXPECT_EQ((*image)(0, 5)[0], 1); // the ids row: grass, rock
        EXPECT_EQ((*image)(0, 5)[1], 2);
    }

    TEST(NeutralSplatStorage, aChunkOfEightCellsIsSplatAndABiggerOneKeepsTheSquareBaseMap)
    {
        Rig rig;
        rig.mSource.mLands[{ kRoot, 0, 0 }] = FakeSource::cell(0, 0);
        rig.start();
        rig.ensure(0, 0, 1, 1);
        const auto bigData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 8.f));
        const auto* big = asImage(bigData);
        ASSERT_NE(big, nullptr);
        EXPECT_EQ(big->width(), 272u);
        EXPECT_EQ(big->height(), 288u);
        const auto biggerData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 16.f));
        const auto* bigger = asImage(biggerData);
        ASSERT_NE(bigger, nullptr);
        EXPECT_EQ(bigger->width(), 256u); // 16 texels a cell, as without the splat
        EXPECT_EQ(bigger->height(), 256u);
        EXPECT_EQ((*bigger)(2, 2)[0], 1); // south-west quadrant of the loaded cell: grass
    }

    TEST(NeutralSplatCache, layersAreDroppedWhenTheCacheIsToldNotToKeepThem)
    {
        FakeSource source;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        c.layers[0].push_back(layer(kRock, { { 1, 255 } }));
        source.mLands[{ kRoot, 0, 0 }] = c;
        CacheLimits limits;
        limits.mKeepLayers = false;
        CellCache dropping(source, limits);
        std::string error;
        ASSERT_TRUE(dropping.setWorld(kRoot, error)) << error;
        dropping.ensureCell(0, 0);
        const auto cell = dropping.get(0, 0);
        ASSERT_NE(cell, nullptr);
        EXPECT_TRUE(cell->layers[0].empty());
        EXPECT_TRUE(cell->hasHeights);

        CellCache keeping(source);
        ASSERT_TRUE(keeping.setWorld(kRoot, error)) << error;
        keeping.ensureCell(0, 0);
        ASSERT_NE(keeping.get(0, 0), nullptr);
        EXPECT_EQ(keeping.get(0, 0)->layers[0].size(), 1u);
    }

    TEST(NeutralSplatStorage, aLayerTextureBeyondTheLayerArrayIsTheDefault)
    {
        Rig rig;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        c.layers[0].push_back(layer(424242, { { 1, 255 } })); // not in the table
        rig.mSource.mLands[{ kRoot, 0, 0 }] = c;
        rig.start();
        rig.ensure(0, 0, 0, 0);
        const auto imageData = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = asImage(imageData);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ((*image)(0, 34)[1], 0);
        EXPECT_EQ((*image)(1, 0)[1], 255);
    }

    TEST(NeutralSplatCache, aCellWithAMalformedLayerIsRefused)
    {
        CacheLimits limits;
        std::string error;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        EXPECT_TRUE(validateTerrainCell(c, 33, limits, error)) << error;
        c.layers[1].push_back(layer(kRock, { { 1, 255 } }));
        EXPECT_TRUE(validateTerrainCell(c, 33, limits, error)) << error;
        c.layers[1].back().opacity.resize(5); // not 17 x 17
        EXPECT_FALSE(validateTerrainCell(c, 33, limits, error));
        EXPECT_NE(error.find("layer"), std::string::npos) << error;
        c.layers[1].back().opacity.clear();
        EXPECT_FALSE(validateTerrainCell(c, 33, limits, error));
    }

    TEST(NeutralSplatCache, aCellWithTooManyLayersInAQuadrantIsRefused)
    {
        CacheLimits limits;
        limits.mMaxLayersPerQuadrant = 3;
        std::string error;
        sdk::TerrainCell c = FakeSource::cell(0, 0);
        for (int i = 0; i < 3; ++i)
            c.layers[2].push_back(layer(kRock, { { i, 255 } }));
        EXPECT_TRUE(validateTerrainCell(c, 33, limits, error)) << error;
        c.layers[2].push_back(layer(kRock, { { 9, 255 } }));
        EXPECT_FALSE(validateTerrainCell(c, 33, limits, error));
    }

    TEST(NeutralSplatStorage, readingNeverNeedsMoreThanTheCache)
    {
        Rig rig;
        rig.mSource.mLands[{ kRoot, 0, 0 }] = FakeSource::cell(0, 0);
        rig.start();
        rig.ensure(0, 0, 0, 0);
        const auto before = rig.mCache->unloadedReads();
        (void)rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        EXPECT_EQ(rig.mCache->unloadedReads(), before);
    }
}
