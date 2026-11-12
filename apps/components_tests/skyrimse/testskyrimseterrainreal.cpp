// Task 0120, stage E1: the neutral terrain storage on the player's own Skyrim SE masters. Prints aggregates only (lines
// starting SKYRIMSE-TERRAIN), never a path, and writes nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required)
//
// It checks, for a block of Tamriel cells around Whiterun, that what NeutralTerrain::Storage serves equals the pack's
// independently decoded LAND records (heights, colours, the decoded normals), that the normals agree with the slope of
// the heights (which settles the sign and axis of VNML), that the chunks of neighbouring cells share their edges
// exactly, that a coarse multi-cell chunk is continuous with the single-cell ones, and that WhiterunWorld borrows the
// land it has none of from Tamriel.
#include <game_skyrimse/terrain_source.hpp>

#include <components/neutralterrain/cellcache.hpp>
#include <components/neutralterrain/storage.hpp>
#include <components/terrain/bounds.hpp>

#include <openeden/game/world_source.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdk = openeden::sdk::game;
    using namespace game_skyrimse;
    using Grid = std::pair<std::int32_t, std::int32_t>;

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    // The SDK's terrain functions over an index, nothing else.
    class IndexSource final : public sdk::WorldCellSource
    {
    public:
        explicit IndexSource(SkyrimTerrainIndex& index)
            : mIndex(index)
        {
        }
        std::string_view gameId() const override { return "skyrimse"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<sdk::WorldId>&) const override { return 0; }
        bool buildCell(sdk::WorldId, sdk::WorldCell&, std::string&) override { return false; }
        bool worldInfo(sdk::WorldId w, sdk::WorldSpaceInfo& o) const override { return mIndex.worldInfo(w, o); }
        sdk::TerrainResult buildTerrain(
            sdk::WorldId w, std::int32_t x, std::int32_t y, sdk::TerrainCell& o, std::string& e) override
        {
            return mIndex.buildTerrain(w, x, y, o, e);
        }
        std::size_t listGroundTextures(std::vector<sdk::GroundTexture>& o) const override
        {
            return mIndex.listGroundTextures(o);
        }

    private:
        SkyrimTerrainIndex& mIndex;
    };

    Terrain::Bounds bounds(float x, float y, float size)
    {
        Terrain::Bounds b;
        b.min = vsg::vec2(x, y);
        b.size = size;
        return b;
    }

    TEST(SkyrimTerrainReal, tamrielAndWhiterunOnTheInstalledMasters)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);

        LoadOrder order;
        std::string err;
        for (const char* name : kMasters)
            ASSERT_TRUE(order.add(name, false, err)) << err;

        SkyrimTerrainIndex index;
        for (const char* name : kMasters)
        {
            std::string error;
            ASSERT_TRUE(index.addPluginFile(data / name, order, error)) << error;
        }
        const auto stats = index.stats();
        std::cout << "SKYRIMSE-TERRAIN index plugins " << stats.mPlugins << " worlds " << stats.mWorlds << " lands "
                  << stats.mLands << " ground textures " << stats.mGroundTextures << " errors "
                  << index.errors().size() << "\n";
        EXPECT_EQ(stats.mPlugins, 5u);
        EXPECT_GT(stats.mLands, 50000u);

        std::vector<sdk::WorldSpaceInfo> worlds;
        index.listWorlds(worlds);
        sdk::WorldId tamriel = 0, whiterun = 0;
        for (const auto& w : worlds)
        {
            if (w.editorId == "Tamriel")
                tamriel = w.id;
            if (w.editorId == "WhiterunWorld")
                whiterun = w.id;
        }
        ASSERT_NE(tamriel, 0u);
        ASSERT_NE(whiterun, 0u);

        // The independent decode: the later master's record wins, as in the index.
        const std::int32_t x0 = 3, y0 = -4, x1 = 7, y1 = -1;
        std::map<Grid, LandRecord> lands;
        for (const char* name : kMasters)
        {
            ExteriorData d;
            Content::Error error;
            ASSERT_TRUE(parseExteriorFile(data / name, order, ExteriorLimits(), d, error)) << error.describe();
            for (LandRecord& land : d.mLands)
                if (land.mWorld == tamriel && land.mGrid && land.mGrid->x >= x0 && land.mGrid->x <= x1
                    && land.mGrid->y >= y0 && land.mGrid->y <= y1)
                    lands[{ land.mGrid->x, land.mGrid->y }] = std::move(land);
        }
        ASSERT_GE(lands.size(), 12u);

        IndexSource source(index);
        auto cache = std::make_shared<NeutralTerrain::CellCache>(source);
        std::string error;
        ASSERT_TRUE(cache->setWorld(tamriel, error)) << error;
        EXPECT_FLOAT_EQ(cache->world().cellSizeUnits, 4096.f);
        EXPECT_EQ(cache->world().vertsPerSide, 33u);
        cache->ensure(x0, y0, x1, y1);
        NeutralTerrain::Storage storage(cache);
        EXPECT_FLOAT_EQ(storage.cellWorldSize, 4096.f);
        EXPECT_EQ(storage.cellVertices, 33u);

        std::uint64_t heightsChecked = 0, heightMismatch = 0, colourMismatch = 0, normalMismatch = 0;
        std::uint64_t normalsChecked = 0, normalsUp = 0, edgesChecked = 0, edgesDiffer = 0, storedEdgeDiffers = 0;
        double slopeErrorSum = 0;
        std::uint64_t slopeSamples = 0;
        std::map<Grid, Terrain::Storage::VertexData> chunks;

        for (const auto& [grid, land] : lands)
        {
            if (!land.mHasHeights)
                continue;
            const auto chunk = storage.getVertexData(0, bounds(static_cast<float>(grid.first), static_cast<float>(grid.second), 1.f));
            chunks[grid] = chunk;
            ASSERT_EQ(chunk.heights->width(), 33u);
            for (int vy = 0; vy < 32; ++vy)
                for (int vx = 0; vx < 32; ++vx)
                {
                    ++heightsChecked;
                    if ((*chunk.heights)(vx, vy) != land.mHeights[vy * 33 + vx])
                        ++heightMismatch;
                    if (land.mHasColours)
                    {
                        const std::size_t i = (static_cast<std::size_t>(vy) * 33 + vx) * 3;
                        if ((*chunk.colors)(vx, vy)
                            != vsg::ubvec4(land.mColours[i], land.mColours[i + 1], land.mColours[i + 2], 255))
                            ++colourMismatch;
                    }
                    if (land.mHasNormals)
                    {
                        const std::size_t i = (static_cast<std::size_t>(vy) * 33 + vx) * 3;
                        vsg::vec3 n(land.mNormals[i], land.mNormals[i + 1], land.mNormals[i + 2]);
                        n = vsg::normalize(n);
                        const vsg::vec4 got = (*chunk.normals)(vx, vy);
                        ++normalsChecked;
                        if (std::fabs(got.x - n.x) > 1e-5f || std::fabs(got.y - n.y) > 1e-5f || std::fabs(got.z - n.z) > 1e-5f)
                            ++normalMismatch;
                        if (got.z > 0.f)
                            ++normalsUp;
                        // the slope of the heights, by central differences inside the cell
                        if (vx >= 1 && vy >= 1 && vx < 31 && vy < 31)
                        {
                            const float dzdx = (land.mHeights[vy * 33 + vx + 1] - land.mHeights[vy * 33 + vx - 1]) / 256.f;
                            const float dzdy = (land.mHeights[(vy + 1) * 33 + vx] - land.mHeights[(vy - 1) * 33 + vx]) / 256.f;
                            const vsg::vec3 s = vsg::normalize(vsg::vec3(-dzdx, -dzdy, 1.f));
                            const float dot = std::min(1.f, std::max(-1.f, vsg::dot(s, vsg::vec3(got.x, got.y, got.z))));
                            slopeErrorSum += std::acos(dot);
                            ++slopeSamples;
                        }
                    }
                }
        }
        EXPECT_GT(heightsChecked, 10000u);
        EXPECT_EQ(heightMismatch, 0u);
        EXPECT_EQ(colourMismatch, 0u);
        EXPECT_EQ(normalMismatch, 0u);
        // the normals face up and follow the terrain's slope (a wrong sign or axis would not)
        ASSERT_GT(normalsChecked, 0u);
        EXPECT_GT(static_cast<double>(normalsUp) / static_cast<double>(normalsChecked), 0.999);
        const double meanSlopeErrorDegrees = slopeSamples ? slopeErrorSum / static_cast<double>(slopeSamples) * 57.29578 : 0;
        EXPECT_LT(meanSlopeErrorDegrees, 12.0);

        // Neighbouring chunks share their edge vertices exactly; the stored edges of the two cells may differ.
        for (const auto& [grid, a] : chunks)
        {
            const auto east = chunks.find({ grid.first + 1, grid.second });
            if (east != chunks.end())
                for (int vy = 0; vy < 33; ++vy)
                {
                    ++edgesChecked;
                    if ((*a.heights)(32, vy) != (*east->second.heights)(0, vy))
                        ++edgesDiffer;
                    if (lands.at(grid).mHeights[vy * 33 + 32] != lands.at({ grid.first + 1, grid.second }).mHeights[vy * 33])
                        ++storedEdgeDiffers;
                }
            const auto north = chunks.find({ grid.first, grid.second + 1 });
            if (north != chunks.end())
                for (int vx = 0; vx < 33; ++vx)
                {
                    ++edgesChecked;
                    if ((*a.heights)(vx, 32) != (*north->second.heights)(vx, 0))
                        ++edgesDiffer;
                    if (lands.at(grid).mHeights[32 * 33 + vx] != lands.at({ grid.first, grid.second + 1 }).mHeights[vx])
                        ++storedEdgeDiffers;
                }
        }
        EXPECT_GT(edgesChecked, 0u);
        EXPECT_EQ(edgesDiffer, 0u);

        // A coarse chunk over four cells is continuous with the cell chunks: its (lod 2) vertex at a cell corner is that cell's.
        {
            const auto big = storage.getVertexData(2, bounds(4.f, -3.f, 2.f));
            ASSERT_EQ(big.heights->width(), 17u);
            std::uint64_t bigMismatch = 0;
            for (int vy = 0; vy < 17; ++vy)
                for (int vx = 0; vx < 17; ++vx)
                {
                    const std::int32_t gx = vx * 4;
                    const std::int32_t gy = vy * 4;
                    // a vertex on a cell edge belongs to the next cell (its local 0): the floor rule
                    const std::int32_t cx = 4 + gx / 32;
                    const std::int32_t cy = -3 + gy / 32;
                    const auto found = chunks.find({ cx, cy });
                    if (found == chunks.end())
                        continue;
                    const int lx = gx % 32;
                    const int ly = gy % 32;
                    if ((*big.heights)(vx, vy) != (*found->second.heights)(lx, ly))
                        ++bigMismatch;
                }
            EXPECT_EQ(bigMismatch, 0u);
        }

        // The chain: WhiterunWorld has land of its own in a few Tamriel cells and borrows the rest.
        {
            auto wcache = std::make_shared<NeutralTerrain::CellCache>(source);
            ASSERT_TRUE(wcache->setWorld(whiterun, error)) << error;
            EXPECT_NE(wcache->world().parentUsage & sdk::TerrainParentUsage::Land, 0u);
            EXPECT_EQ(wcache->world().parent, tamriel);
            wcache->ensure(3, -4, 7, -1);
            std::size_t own = 0, borrowed = 0, none = 0;
            for (std::int32_t y = -4; y <= -1; ++y)
                for (std::int32_t x = 3; x <= 7; ++x)
                {
                    const auto cell = wcache->get(x, y);
                    if (!cell)
                        ++none;
                    else if (cell->world == whiterun)
                        ++own;
                    else if (cell->world == tamriel)
                        ++borrowed;
                }
            std::cout << "SKYRIMSE-TERRAIN whiterun own " << own << " borrowed " << borrowed << " none " << none << "\n";
            EXPECT_GE(own, 1u);
            EXPECT_GE(borrowed, 1u);
        }

        std::cout << "SKYRIMSE-TERRAIN heights checked " << heightsChecked << " mismatched " << heightMismatch
                  << " colours mismatched " << colourMismatch << " normals checked " << normalsChecked
                  << " mismatched " << normalMismatch << " facing up " << normalsUp << " mean slope error degrees "
                  << meanSlopeErrorDegrees << " shared edge vertices " << edgesChecked << " differing "
                  << edgesDiffer << " (stored edges that differ " << storedEdgeDiffers << ") cells "
                  << lands.size() << " unloaded reads " << cache->unloadedReads() << " refused " << cache->refusedCells()
                  << "\n";
    }
}
