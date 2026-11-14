// Task 0120, stage E1: the neutral terrain cache and storage on a synthetic source. No game data.
#include <components/neutralterrain/cellcache.hpp>
#include <components/neutralterrain/storage.hpp>

#include <components/terrain/bounds.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <tuple>

namespace
{
    using namespace openeden::sdk::game;
    using NeutralTerrain::CacheLimits;
    using NeutralTerrain::CellCache;
    using NeutralTerrain::Storage;

    constexpr WorldId kRoot = 10;
    constexpr WorldId kChild = 11;
    constexpr WorldId kTexGrass = 100;
    constexpr WorldId kTexRock = 101;

    // The synthetic land: a vertex (lx, ly) of cell (cx, cy) has height 1 * (cx * 32 + lx) + 1000 * (cy * 32 + ly) +
    // the cell's own bias, so a wrong axis, a wrong cell or a wrong offset shows at once.
    float expected(std::int32_t cx, std::int32_t cy, int lx, int ly, float bias = 0.f)
    {
        return static_cast<float>(cx * 32 + lx) + 1000.f * static_cast<float>(cy * 32 + ly) + bias;
    }

    class FakeSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fake"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& e) override
        {
            e = "none";
            return false;
        }

        bool worldInfo(WorldId id, WorldSpaceInfo& out) const override
        {
            const auto found = mWorlds.find(id);
            if (found == mWorlds.end())
                return false;
            out = found->second;
            return true;
        }
        TerrainResult buildTerrain(
            WorldId world, std::int32_t x, std::int32_t y, TerrainCell& out, std::string& error) override
        {
            ++mBuildCalls;
            mMainThreadOnly = mMainThreadOnly && std::this_thread::get_id() == mOwner;
            if (mFailing.count({ world, x, y }) != 0)
            {
                error = "boom";
                return TerrainResult::Failed;
            }
            const auto found = mLands.find({ world, x, y });
            if (found == mLands.end())
                return TerrainResult::NoLand;
            out = found->second;
            return TerrainResult::Built;
        }
        std::size_t listGroundTextures(std::vector<GroundTexture>& out) const override
        {
            for (const GroundTexture& t : mTextures)
                out.push_back(t);
            return mTextures.size();
        }

        TerrainCell makeCell(WorldId world, std::int32_t x, std::int32_t y, std::uint32_t verts = 33, float bias = 0.f)
        {
            TerrainCell c;
            c.world = world;
            c.gridX = x;
            c.gridY = y;
            c.vertsPerSide = verts;
            c.hasHeights = true;
            c.heights.resize(verts * verts);
            for (std::uint32_t ly = 0; ly < verts; ++ly)
                for (std::uint32_t lx = 0; lx < verts; ++lx)
                    c.heights[ly * verts + lx] = expected(x, y, static_cast<int>(lx), static_cast<int>(ly), bias);
            return c;
        }
        void addLand(TerrainCell cell) { mLands[{ cell.world, cell.gridX, cell.gridY }] = std::move(cell); }

        FakeSource()
        {
            WorldSpaceInfo root;
            root.id = kRoot;
            root.editorId = "Root";
            root.defaultLandHeight = -500.f;
            mWorlds[kRoot] = root;
            WorldSpaceInfo child = root;
            child.id = kChild;
            child.editorId = "Child";
            child.parent = kRoot;
            child.parentUsage = TerrainParentUsage::Land;
            mWorlds[kChild] = child;
            GroundTexture def;
            def.id = 0;
            def.diffusePath = "textures\\landscape\\dirt02.dds";
            mTextures.push_back(def);
            GroundTexture grass;
            grass.id = kTexGrass;
            grass.diffusePath = "textures\\landscape\\grass.dds";
            mTextures.push_back(grass);
            GroundTexture rock;
            rock.id = kTexRock;
            rock.diffusePath = "textures\\landscape\\rock.dds";
            mTextures.push_back(rock);
            mOwner = std::this_thread::get_id();
        }

        std::map<WorldId, WorldSpaceInfo> mWorlds;
        std::map<std::tuple<WorldId, std::int32_t, std::int32_t>, TerrainCell> mLands;
        std::set<std::tuple<WorldId, std::int32_t, std::int32_t>> mFailing;
        std::vector<GroundTexture> mTextures;
        std::atomic<int> mBuildCalls{ 0 };
        std::thread::id mOwner;
        bool mMainThreadOnly = true;
    };

    struct Rig
    {
        FakeSource mSource;
        std::shared_ptr<CellCache> mCache;
        std::unique_ptr<Storage> mStorage;

        void start(WorldId world = kRoot, CacheLimits limits = {})
        {
            mCache = std::make_shared<CellCache>(mSource, limits);
            std::string error;
            ASSERT_TRUE(mCache->setWorld(world, error)) << error;
            mStorage = std::make_unique<Storage>(mCache);
        }
    };

    Terrain::Bounds bounds(float x, float y, float size)
    {
        Terrain::Bounds b;
        b.min = vsg::vec2(x, y);
        b.size = size;
        return b;
    }

    // ---- the cache ------------------------------------------------------------------------------------------------

    TEST(NeutralTerrainCache, setWorldReadsTheWorldAndTheTextureTable)
    {
        Rig rig;
        rig.start();
        EXPECT_TRUE(rig.mCache->hasWorld());
        EXPECT_EQ(rig.mCache->world().id, kRoot);
        EXPECT_EQ(rig.mCache->world().editorId, "Root");
        ASSERT_EQ(rig.mCache->textures().size(), 3u);
        EXPECT_EQ(rig.mCache->textures()[0].id, 0u);
        EXPECT_EQ(rig.mCache->textureIndex(kTexRock), 2u);
        EXPECT_EQ(rig.mCache->textureIndex(9999), 0u); // an unknown id is the default
    }

    TEST(NeutralTerrainCache, anUnknownWorldIsRefusedWithAMessage)
    {
        FakeSource source;
        CellCache cache(source);
        std::string error;
        EXPECT_FALSE(cache.setWorld(777, error));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(cache.hasWorld());
    }

    TEST(NeutralTerrainCache, aSourceWithoutTerrainIsRefused)
    {
        class Bare final : public WorldCellSource
        {
        public:
            std::string_view gameId() const override { return "bare"; }
            std::size_t cellCount() const override { return 0; }
            std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
            bool buildCell(WorldId, WorldCell&, std::string&) override { return false; }
        } bare;
        CellCache cache(bare);
        std::string error;
        EXPECT_FALSE(cache.setWorld(kRoot, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralTerrainCache, aCellIsLoadedOnceAndPublishedAsASnapshot)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 3, -2));
        rig.start();
        EXPECT_FALSE(rig.mCache->isKnown(3, -2));
        EXPECT_EQ(rig.mCache->get(3, -2), nullptr);
        EXPECT_EQ(rig.mCache->unloadedReads(), 1u);
        EXPECT_TRUE(rig.mCache->ensureCell(3, -2));
        EXPECT_TRUE(rig.mCache->ensureCell(3, -2));
        EXPECT_EQ(rig.mSource.mBuildCalls.load(), 1);
        const auto cell = rig.mCache->get(3, -2);
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->gridX, 3);
        EXPECT_EQ(cell->heights.size(), 33u * 33u);
        EXPECT_EQ(rig.mCache->builtCells(), 1u);
    }

    TEST(NeutralTerrainCache, aCellWithoutLandIsKnownAndEmpty)
    {
        Rig rig;
        rig.start();
        EXPECT_TRUE(rig.mCache->ensureCell(50, 50));
        EXPECT_TRUE(rig.mCache->isKnown(50, 50));
        EXPECT_EQ(rig.mCache->get(50, 50), nullptr);
        EXPECT_EQ(rig.mCache->unloadedReads(), 0u);
    }

    TEST(NeutralTerrainCache, landTheWorldBorrowsComesFromTheParentOnlyWithTheUsageBit)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 4, 4, 33, 7.f));
        rig.mSource.addLand(rig.mSource.makeCell(kChild, 5, 5, 33, 9.f));
        rig.start(kChild);
        rig.mCache->ensure(4, 4, 5, 5);
        const auto borrowed = rig.mCache->get(4, 4);
        ASSERT_NE(borrowed, nullptr);
        EXPECT_EQ(borrowed->world, kRoot);
        EXPECT_FLOAT_EQ(borrowed->heights[0], expected(4, 4, 0, 0, 7.f));
        const auto own = rig.mCache->get(5, 5);
        ASSERT_NE(own, nullptr);
        EXPECT_EQ(own->world, kChild);
        EXPECT_EQ(rig.mCache->get(4, 5), nullptr);

        Rig other;
        other.mSource.mWorlds[kChild].parentUsage = 0x08; // lends water, not land
        other.mSource.addLand(other.mSource.makeCell(kRoot, 4, 4));
        other.start(kChild);
        other.mCache->ensureCell(4, 4);
        EXPECT_TRUE(other.mCache->isKnown(4, 4));
        EXPECT_EQ(other.mCache->get(4, 4), nullptr);
    }

    TEST(NeutralTerrainCache, aParentCycleEndsAtTheDepthLimit)
    {
        Rig rig;
        rig.mSource.mWorlds[kRoot].parent = kChild; // root <-> child
        rig.mSource.mWorlds[kRoot].parentUsage = TerrainParentUsage::Land;
        CacheLimits limits;
        limits.mMaxParentDepth = 3;
        rig.start(kChild, limits);
        EXPECT_TRUE(rig.mCache->ensureCell(0, 0));
        EXPECT_EQ(rig.mCache->get(0, 0), nullptr);
        EXPECT_LE(rig.mSource.mBuildCalls.load(), 4);
    }

    TEST(NeutralTerrainCache, aHostileCellIsRefusedCountedAndTreatedAsNoLand)
    {
        Rig rig;
        TerrainCell wrongSize = rig.mSource.makeCell(kRoot, 0, 0, 17); // 17 vertices in a 33 world
        rig.mSource.addLand(wrongSize);
        TerrainCell nan = rig.mSource.makeCell(kRoot, 1, 0);
        nan.heights[40] = std::numeric_limits<float>::quiet_NaN();
        rig.mSource.addLand(nan);
        TerrainCell shortHeights = rig.mSource.makeCell(kRoot, 2, 0);
        shortHeights.heights.resize(10);
        rig.mSource.addLand(shortHeights);
        TerrainCell badNormals = rig.mSource.makeCell(kRoot, 4, 0);
        badNormals.hasNormals = true;
        badNormals.normals.assign(5, 0);
        rig.mSource.addLand(badNormals);
        rig.mSource.mFailing.insert({ kRoot, 5, 0 });
        rig.start();
        rig.mCache->ensure(0, 0, 5, 0);
        for (std::int32_t x = 0; x <= 5; ++x)
        {
            EXPECT_TRUE(rig.mCache->isKnown(x, 0)) << x;
            EXPECT_EQ(rig.mCache->get(x, 0), nullptr) << x;
        }
        EXPECT_EQ(rig.mCache->refusedCells(), 5u);
        EXPECT_EQ(rig.mCache->errors().size(), 5u);
        EXPECT_NE(rig.mCache->errors()[0].find("cell (0, 0)"), std::string::npos) << rig.mCache->errors()[0];
    }

    TEST(NeutralTerrainCache, validateAcceptsAGoodCellAndNamesTheFirstFault)
    {
        FakeSource source;
        TerrainCell good = source.makeCell(kRoot, 1, 1);
        std::string error;
        EXPECT_TRUE(NeutralTerrain::validateTerrainCell(good, 33, CacheLimits(), error)) << error;
        good.colours.assign(3, 0);
        good.hasColours = true;
        EXPECT_FALSE(NeutralTerrain::validateTerrainCell(good, 33, CacheLimits(), error));
        EXPECT_NE(error.find("colours"), std::string::npos) << error;
    }

    TEST(NeutralTerrainCache, theResidentCapDropsTheOldestAndAHeldSnapshotSurvives)
    {
        Rig rig;
        for (std::int32_t x = 0; x < 10; ++x)
            rig.mSource.addLand(rig.mSource.makeCell(kRoot, x, 0));
        CacheLimits limits;
        limits.mMaxCells = 4;
        rig.start(kRoot, limits);
        rig.mCache->ensureCell(0, 0);
        const auto held = rig.mCache->get(0, 0);
        ASSERT_NE(held, nullptr);
        rig.mCache->ensure(1, 0, 9, 0);
        EXPECT_LE(rig.mCache->resident(), 4u);
        EXPECT_FALSE(rig.mCache->isKnown(0, 0)); // the oldest went
        EXPECT_FLOAT_EQ(held->heights[33], expected(0, 0, 0, 1)); // but a worker holding it still reads it
        EXPECT_TRUE(rig.mCache->isKnown(9, 0));
    }

    TEST(NeutralTerrainCache, oneEnsureCallIsCappedByArea)
    {
        Rig rig;
        CacheLimits limits;
        limits.mMaxEnsureCells = 16;
        rig.start(kRoot, limits);
        EXPECT_EQ(rig.mCache->ensure(0, 0, 99, 99), 0u);
        EXPECT_EQ(rig.mSource.mBuildCalls.load(), 0);
        EXPECT_EQ(rig.mCache->ensure(0, 0, 3, 3), 16u);
        EXPECT_EQ(rig.mCache->ensure(5, 5, 4, 4), 0u); // an inverted range is nothing
    }

    TEST(NeutralTerrainCache, theSourceIsOnlyAskedOnTheCallingThreadAndWorkersReadSnapshots)
    {
        Rig rig;
        for (std::int32_t x = 0; x < 8; ++x)
            rig.mSource.addLand(rig.mSource.makeCell(kRoot, x, 0));
        rig.start();
        rig.mCache->ensure(0, 0, 7, 0);
        const int calls = rig.mSource.mBuildCalls.load();
        std::atomic<int> wrong{ 0 };
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([&] {
                for (int i = 0; i < 2000; ++i)
                {
                    const auto c = rig.mCache->get(i % 8, 0);
                    if (!c || c->heights[0] != expected(i % 8, 0, 0, 0))
                        ++wrong;
                }
            });
        for (auto& w : workers)
            w.join();
        EXPECT_EQ(wrong.load(), 0);
        EXPECT_EQ(rig.mSource.mBuildCalls.load(), calls);
        EXPECT_TRUE(rig.mSource.mMainThreadOnly);
    }

    // ---- the storage ------------------------------------------------------------------------------------------------

    TEST(NeutralTerrainStorage, theCellSizeAndVerticesComeFromTheWorld)
    {
        Rig rig;
        rig.start();
        EXPECT_FLOAT_EQ(rig.mStorage->cellWorldSize, 4096.f);
        EXPECT_EQ(rig.mStorage->cellVertices, 33u);
        EXPECT_EQ(rig.mStorage->numVerts(1.f, 0), 33u);
        EXPECT_EQ(rig.mStorage->numVerts(0.125f, 0), 5u);

        Rig small;
        small.mSource.mWorlds[kRoot].cellSizeUnits = 2048.f;
        small.mSource.mWorlds[kRoot].vertsPerSide = 17;
        small.start();
        EXPECT_FLOAT_EQ(small.mStorage->cellWorldSize, 2048.f);
        EXPECT_EQ(small.mStorage->cellVertices, 17u);
    }

    TEST(NeutralTerrainStorage, oneCellAtFullDetailServesItsHeightsColumnIsXAndRowIsY)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 2, 3));
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 3, 3));
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 2, 4));
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 3, 4));
        rig.start();
        rig.mCache->ensure(2, 3, 3, 4);
        const auto data = rig.mStorage->getVertexData(0, bounds(2.f, 3.f, 1.f));
        ASSERT_TRUE(data.heights);
        ASSERT_EQ(data.heights->width(), 33u);
        ASSERT_EQ(data.heights->height(), 33u);
        for (int vy = 0; vy < 33; ++vy)
            for (int vx = 0; vx < 33; ++vx)
                ASSERT_FLOAT_EQ((*data.heights)(vx, vy), expected(2, 3, vx, vy)) << vx << "," << vy;
    }

    TEST(NeutralTerrainStorage, theLastVertexOfAChunkIsTheNextCellsFirstSoNeighboursShareAnEdge)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 0, 0, 33, 0.f));
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 1, 0, 33, 0.5f)); // its stored west edge differs from A's east
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 0, 1));
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 1, 1));
        rig.start();
        rig.mCache->ensure(0, 0, 1, 1);
        const auto a = rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        const auto b = rig.mStorage->getVertexData(0, bounds(1.f, 0.f, 1.f));
        for (int vy = 0; vy < 32; ++vy)
            EXPECT_FLOAT_EQ((*a.heights)(32, vy), (*b.heights)(0, vy)) << vy;
        EXPECT_FLOAT_EQ((*a.heights)(32, 5), expected(1, 0, 0, 5, 0.5f)); // taken from B, not from A's own edge
        EXPECT_FLOAT_EQ((*a.heights)(31, 5), expected(0, 0, 31, 5));
        // and the north edge likewise
        const auto north = rig.mStorage->getVertexData(0, bounds(0.f, 1.f, 1.f));
        for (int vx = 0; vx < 32; ++vx)
            EXPECT_FLOAT_EQ((*a.heights)(vx, 32), (*north.heights)(vx, 0)) << vx;
    }

    TEST(NeutralTerrainStorage, anEdgeVertexFallsBackToTheNeighbourThatHasLand)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 0, 0)); // nothing to its east or north
        rig.start();
        rig.mCache->ensure(0, 0, 1, 1);
        const auto a = rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        EXPECT_FLOAT_EQ((*a.heights)(32, 7), expected(0, 0, 32, 7)); // its own last column, no cliff to the default
        EXPECT_FLOAT_EQ((*a.heights)(7, 32), expected(0, 0, 7, 32));
        EXPECT_FLOAT_EQ((*a.heights)(32, 32), expected(0, 0, 32, 32));
    }

    TEST(NeutralTerrainStorage, aCoarseLodTakesEveryOtherVertex)
    {
        Rig rig;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                rig.mSource.addLand(rig.mSource.makeCell(kRoot, x, y));
        rig.start();
        rig.mCache->ensure(0, 0, 1, 1);
        const auto lod1 = rig.mStorage->getVertexData(1, bounds(0.f, 0.f, 1.f));
        ASSERT_EQ(lod1.heights->width(), 17u);
        for (int vy = 0; vy < 17; ++vy)
            for (int vx = 0; vx < 17; ++vx)
                ASSERT_FLOAT_EQ((*lod1.heights)(vx, vy), expected(0, 0, vx * 2, vy * 2)) << vx << "," << vy;
        const auto lod3 = rig.mStorage->getVertexData(3, bounds(0.f, 0.f, 1.f));
        ASSERT_EQ(lod3.heights->width(), 5u);
        EXPECT_FLOAT_EQ((*lod3.heights)(4, 4), expected(1, 1, 0, 0)); // the far corner is the next cell's origin
    }

    TEST(NeutralTerrainStorage, aChunkSmallerThanACellStartsInsideIt)
    {
        Rig rig;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                rig.mSource.addLand(rig.mSource.makeCell(kRoot, x, y));
        rig.start();
        rig.mCache->ensure(0, 0, 1, 1);
        const auto d = rig.mStorage->getVertexData(0, bounds(0.5f, 0.25f, 0.125f)); // 4 intervals
        ASSERT_EQ(d.heights->width(), 5u);
        for (int vy = 0; vy < 5; ++vy)
            for (int vx = 0; vx < 5; ++vx)
                ASSERT_FLOAT_EQ((*d.heights)(vx, vy), expected(0, 0, 16 + vx, 8 + vy)) << vx << "," << vy;
    }

    TEST(NeutralTerrainStorage, aChunkOfSeveralCellsIsContinuousAcrossTheirBorders)
    {
        Rig rig;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                rig.mSource.addLand(rig.mSource.makeCell(kRoot, x, y));
        rig.start();
        rig.mCache->ensure(0, 0, 1, 1);
        const auto d = rig.mStorage->getVertexData(1, bounds(0.f, 0.f, 2.f));
        ASSERT_EQ(d.heights->width(), 33u); // 32 * 2 / 2 + 1
        for (int vy = 0; vy < 33; ++vy)
            for (int vx = 0; vx < 33; ++vx)
            {
                const int gx = vx * 2;
                const int gy = vy * 2;
                const int cx = gx / 32 > 1 ? 1 : gx / 32;
                const int cy = gy / 32 > 1 ? 1 : gy / 32;
                const int lx = gx - cx * 32;
                const int ly = gy - cy * 32;
                ASSERT_FLOAT_EQ((*d.heights)(vx, vy), expected(cx, cy, lx, ly)) << vx << "," << vy;
            }
    }

    TEST(NeutralTerrainStorage, aMissingCellReadsAsTheWorldsDefaultHeightFlatAndWhite)
    {
        Rig rig;
        rig.start();
        rig.mCache->ensure(
            8, 8, 10, 10); // a chunk also reads its east and north cells, and the south-west ones when it has no land
        const auto d = rig.mStorage->getVertexData(0, bounds(9.f, 9.f, 1.f));
        EXPECT_FLOAT_EQ((*d.heights)(10, 10), -500.f);
        EXPECT_FLOAT_EQ((*d.normals)(10, 10).z, 1.f);
        EXPECT_EQ((*d.colors)(10, 10), vsg::ubvec4(255, 255, 255, 255));
        EXPECT_EQ(rig.mCache->unloadedReads(), 0u);
        const auto notLoaded = rig.mStorage->getVertexData(0, bounds(30.f, 30.f, 1.f));
        EXPECT_FLOAT_EQ((*notLoaded.heights)(0, 0), -500.f);
        EXPECT_GT(rig.mCache->unloadedReads(), 0u);
    }

    TEST(NeutralTerrainStorage, aCellWithoutHeightDataIsFlatAtTheDefault)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.hasHeights = false;
        c.heights.clear();
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto d = rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        EXPECT_FLOAT_EQ((*d.heights)(3, 4), -500.f);
    }

    TEST(NeutralTerrainStorage, normalsAreDecodedFromSignedBytesAndNormalised)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.hasNormals = true;
        c.normals.assign(33 * 33 * 3, 0);
        for (int i = 0; i < 33 * 33; ++i)
            c.normals[static_cast<std::size_t>(i) * 3 + 2] = 127;
        const int at = (4 * 33 + 6) * 3; // row 4 (y), column 6 (x)
        c.normals[at + 0] = 64; // +x
        c.normals[at + 1] = -64; // -y
        c.normals[at + 2] = 64;
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto d = rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        const vsg::vec4 n = (*d.normals)(6, 4);
        EXPECT_NEAR(n.x, 0.577f, 0.01f);
        EXPECT_NEAR(n.y, -0.577f, 0.01f);
        EXPECT_NEAR(n.z, 0.577f, 0.01f);
        EXPECT_NEAR((*d.normals)(0, 0).z, 1.f, 1e-4f);
        // a zero normal is straight up, never NaN
        c.normals[at + 0] = 0;
        c.normals[at + 1] = 0;
        c.normals[at + 2] = 0;
        Rig rig2;
        rig2.mSource.addLand(c);
        rig2.start();
        rig2.mCache->ensureCell(0, 0);
        const auto d2 = rig2.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        EXPECT_FLOAT_EQ((*d2.normals)(6, 4).z, 1.f);
    }

    TEST(NeutralTerrainStorage, coloursAreRgbWithFullAlpha)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.hasColours = true;
        c.colours.assign(33 * 33 * 3, 255);
        const int at = (2 * 33 + 9) * 3;
        c.colours[at] = 10;
        c.colours[at + 1] = 20;
        c.colours[at + 2] = 30;
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto d = rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
        EXPECT_EQ((*d.colors)(9, 2), vsg::ubvec4(10, 20, 30, 255));
        EXPECT_EQ((*d.colors)(0, 0), vsg::ubvec4(255, 255, 255, 255));
    }

    TEST(NeutralTerrainStorage, theHeightAtAVertexIsTheVertexAndBetweenThemItIsOnTheTriangle)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 1, 2));
        rig.start();
        rig.mCache->ensureCell(1, 2);
        const float cell = 4096.f;
        const float step = cell / 32.f;
        const float ox = 1 * cell;
        const float oy = 2 * cell;
        EXPECT_NEAR(
            rig.mStorage->getHeightAt(vsg::vec3(ox + 5 * step, oy + 7 * step, 0), {}), expected(1, 2, 5, 7), 0.05f);
        // the synthetic land is a plane (slope 1 in x per vertex, 1000 in y per vertex): any triangle gives the plane
        const float x = ox + 5.25f * step;
        const float y = oy + 7.5f * step;
        EXPECT_NEAR(rig.mStorage->getHeightAt(vsg::vec3(x, y, 0), {}), 1 * 32 + 5.25f + 1000.f * (2 * 32 + 7.5f), 0.1f);
        // outside any loaded land: the default
        EXPECT_FLOAT_EQ(rig.mStorage->getHeightAt(vsg::vec3(-50000.f, 0, 0), {}), -500.f);
        EXPECT_FLOAT_EQ(rig.mStorage->getHeightAt(vsg::vec3(std::nanf(""), 0, 0), {}), -500.f);
    }

    TEST(NeutralTerrainStorage, layersAreTheDefaultFirstThenTheGroundTextures)
    {
        Rig rig;
        rig.start();
        const auto layers = rig.mStorage->getLayers();
        ASSERT_EQ(layers.size(), 3u);
        EXPECT_EQ(layers[0], "textures/landscape/dirt02.dds");
        EXPECT_EQ(layers[1], "textures/landscape/grass.dds");
        EXPECT_EQ(layers[2], "textures/landscape/rock.dds");
    }

    TEST(NeutralTerrainStorage, theBlendMapNamesEachQuadrantsBaseLayerInAllFourChannels)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.baseTextures[0] = kTexGrass; // south-west
        c.baseTextures[1] = kTexRock; // south-east
        c.baseTextures[2] = 0; // north-west: the default
        c.baseTextures[3] = kTexGrass; // north-east
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto map = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = dynamic_cast<vsg::ubvec4Array2D*>(map.get());
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(image->width(), 16u);
        ASSERT_EQ(image->height(), 16u);
        const auto expectAll = [&](int x, int y, unsigned char index) {
            for (int i = 0; i < 4; ++i)
                EXPECT_EQ((*image)(x, y)[i], index) << x << "," << y << " channel " << i;
        };
        expectAll(2, 2, 1); // south-west: grass is layer 1
        expectAll(12, 3, 2); // south-east: rock
        expectAll(3, 12, 0); // north-west: default
        expectAll(13, 13, 1); // north-east: grass
    }

    TEST(NeutralTerrainStorage, aSmallerBoundsGivesTheMatchingPartOfTheBlendMap)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.baseTextures[3] = kTexRock;
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto map = rig.mStorage->getBlendmap(bounds(0.5f, 0.5f, 0.5f)); // the north-east quarter
        const auto* image = dynamic_cast<vsg::ubvec4Array2D*>(map.get());
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(image->width(), 8u);
        EXPECT_EQ((*image)(0, 0)[0], 2);
        EXPECT_EQ((*image)(7, 7)[3], 2);
    }

    TEST(NeutralTerrainStorage, anUnknownTextureIdInACellIsTheDefaultLayer)
    {
        Rig rig;
        TerrainCell c = rig.mSource.makeCell(kRoot, 0, 0);
        c.baseTextures[0] = 424242; // not in the table
        rig.mSource.addLand(c);
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const auto map = rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
        const auto* image = dynamic_cast<vsg::ubvec4Array2D*>(map.get());
        ASSERT_NE(image, nullptr);
        EXPECT_EQ((*image)(1, 1)[0], 0);
    }

    TEST(NeutralTerrainStorage, buildingAChunkNeverCallsTheSource)
    {
        Rig rig;
        rig.mSource.addLand(rig.mSource.makeCell(kRoot, 0, 0));
        rig.start();
        rig.mCache->ensureCell(0, 0);
        const int calls = rig.mSource.mBuildCalls.load();
        std::vector<std::thread> workers;
        for (int t = 0; t < 3; ++t)
            workers.emplace_back([&] {
                for (int i = 0; i < 20; ++i)
                {
                    (void)rig.mStorage->getVertexData(0, bounds(0.f, 0.f, 1.f));
                    (void)rig.mStorage->getBlendmap(bounds(0.f, 0.f, 1.f));
                    (void)rig.mStorage->getHeightAt(vsg::vec3(100.f, 100.f, 0), {});
                }
            });
        for (auto& w : workers)
            w.join();
        EXPECT_EQ(rig.mSource.mBuildCalls.load(), calls);
    }
}
