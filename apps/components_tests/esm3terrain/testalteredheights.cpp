// Tests for stage B4 of Track V stage V6 (docs/tasks/0014-vulkan-v6-editor-tools-osg-removal.md): the "altered
// heights" hooks of ESMTerrain::Storage, restored in components/esm3terrain from the unbuilt OSG version in
// components/esmterrain/storage.cpp.
//
// Behaviour the expected values are worked from, in Storage::fillVertexBuffers of that file:
//   - line 263: the hooks are consulted only when useAlteration() is true.
//   - lines 310-314: height = record height (or DEFAULT_HEIGHT -2048) + getAlteredHeight(col, row).
//   - lines 324-344: normals never look at the altered height; nothing is recalculated.
//   - lines 346-354: adjustColor(col, row, heightData, color) runs on the record colour.
//   - lines 356-358: fixColour runs after adjustColor, so on a cell's last row or column the neighbour's record
//     colour replaces whatever adjustColor did.
//   - the hook takes no cell (storage.hpp line 159), so it cannot tell cells of a multi-cell chunk apart.
//   - getHeightAt (lines 568-650) reads the record only and never applies an alteration.
//
// In the output, vertex (x, y) is heights(x, y) and comes from `row` = x and `col` = y of the cell.

#include <components/esm3terrain/storage.hpp>
#include <components/terrain/bounds.hpp>

#include <vsg/core/Array2D.h>
// MSVC needs Array2D<T>::accept, which these headers define, in this translation unit (as storage.cpp).
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Visitor.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace
{
    constexpr int landSize = ESM::Land::LAND_SIZE; // 65

    // Owns the generated land records the storages read. Lives longer than every LandObject, which keeps a pointer
    // to its record.
    class LandWorld
    {
    public:
        // height   = cellBase + col * 65 + row, cellBase = 10000 * cellX + 20000 * cellY
        // normal   = (row % 5 * 10, col % 5 * 10, 100), before normalizing
        // colour   = (col, row, 10 + 10 * cellX + 20 * cellY)
        // Every cell's values differ, so a test can see which cell and which vertex a value came from. Land heights
        // deliberately do not match across borders here; addFlatCell is for tests about border continuity.
        void addGradedCell(int cellX, int cellY)
        {
            ESM::Land& land = addBlank(cellX, cellY);
            ESM::Land::LandData& data = *land.getLandData();
            for (int col = 0; col < landSize; ++col)
            {
                for (int row = 0; row < landSize; ++row)
                {
                    const int vertex = col * landSize + row;
                    data.mHeights[vertex] = static_cast<float>(10000 * cellX + 20000 * cellY + vertex);
                    data.mNormals[vertex * 3 + 0] = static_cast<std::int8_t>(row % 5 * 10);
                    data.mNormals[vertex * 3 + 1] = static_cast<std::int8_t>(col % 5 * 10);
                    data.mNormals[vertex * 3 + 2] = 100;
                    data.mColours[vertex * 3 + 0] = static_cast<std::uint8_t>(col);
                    data.mColours[vertex * 3 + 1] = static_cast<std::uint8_t>(row);
                    data.mColours[vertex * 3 + 2] = static_cast<std::uint8_t>(10 + 10 * cellX + 20 * cellY);
                }
            }
        }

        void addGradedSquare()
        {
            addGradedCell(0, 0);
            addGradedCell(1, 0);
            addGradedCell(0, 1);
            addGradedCell(1, 1);
        }

        void addFlatCell(int cellX, int cellY, float height)
        {
            addBlank(cellX, cellY).getLandData()->mHeights.fill(height);
        }

        const ESM::Land* find(int cellX, int cellY) const
        {
            const auto it = mLands.find({ cellX, cellY });
            return it == mLands.end() ? nullptr : it->second.get();
        }

    private:
        ESM::Land& addBlank(int cellX, int cellY)
        {
            auto land = std::make_unique<ESM::Land>();
            land->mX = cellX;
            land->mY = cellY;
            land->blank(); // all data types present, no file context: LandObject copies mLandData
            ESM::Land& result = *land;
            mLands[{ cellX, cellY }] = std::move(land);
            return result;
        }

        std::map<std::pair<int, int>, std::unique_ptr<ESM::Land>> mLands;
    };

    // The storage as the game uses it: the hooks keep their defaults.
    class PlainStorage : public ESMTerrain::Storage
    {
    public:
        explicit PlainStorage(const LandWorld& world)
            : mWorld(world)
        {
        }

        vsg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellLocation) override
        {
            const ESM::Land* land = mWorld.find(cellLocation.mX, cellLocation.mY);
            if (land == nullptr)
                return {};
            // vsg::ref_ptr is intrusive and LandObject has no vsg::Inherit create(), so it adopts this new directly.
            return vsg::ref_ptr<const ESMTerrain::LandObject>(new ESMTerrain::LandObject(*land,
                ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VCLR | ESM::Land::DATA_VTEX));
        }

        std::map<PluginTexture, std::string> enumerateLayers() const override { return {}; }

    private:
        const LandWorld& mWorld;
    };

    // useAlteration() is true but the other hooks keep their base class defaults, which do nothing.
    class DefaultHooksStorage : public PlainStorage
    {
    public:
        using PlainStorage::PlainStorage;

    private:
        bool useAlteration() const override { return true; }
    };

    // A subclass in the way the editor would write one: per-vertex offsets and colour changes for its cell.
    class AlteringStorage : public PlainStorage
    {
    public:
        using Vertex = std::pair<int, int>; // col, row

        explicit AlteringStorage(const LandWorld& world, bool alteration = true)
            : PlainStorage(world)
            , mAlteration(alteration)
        {
        }

        std::map<Vertex, float> mHeightOffsets;
        std::map<Vertex, std::uint8_t> mRedOverrides;

        mutable int mHeightCalls = 0;
        mutable int mColourCalls = 0;

        // What adjustColor was given for the vertices in mRedOverrides.
        mutable std::optional<vsg::ubvec4> mColourSeen;
        mutable std::optional<float> mRecordHeightSeen;
        mutable bool mHeightDataWasNull = false;

    private:
        bool useAlteration() const override { return mAlteration; }

        float getAlteredHeight(int col, int row) const override
        {
            ++mHeightCalls;
            const auto it = mHeightOffsets.find({ col, row });
            return it == mHeightOffsets.end() ? 0.f : it->second;
        }

        void adjustColor(int col, int row, const ESM::Land::LandData* heightData, vsg::ubvec4& color) const override
        {
            ++mColourCalls;
            const auto it = mRedOverrides.find({ col, row });
            if (it == mRedOverrides.end())
                return;
            mColourSeen = color;
            mHeightDataWasNull = heightData == nullptr;
            if (heightData != nullptr)
                mRecordHeightSeen = heightData->mHeights[col * landSize + row];
            color[0] = it->second;
        }

        bool mAlteration;
    };

    Terrain::Bounds makeBounds(float minX, float minY, float size)
    {
        Terrain::Bounds bounds;
        bounds.size = size;
        bounds.min = vsg::vec2(minX, minY);
        return bounds;
    }

    int differingHeights(const Terrain::Storage::VertexData& a, const Terrain::Storage::VertexData& b)
    {
        int count = 0;
        for (std::uint32_t y = 0; y < a.heights->height(); ++y)
            for (std::uint32_t x = 0; x < a.heights->width(); ++x)
                if ((*a.heights)(x, y) != (*b.heights)(x, y))
                    ++count;
        return count;
    }

    int differingNormals(const Terrain::Storage::VertexData& a, const Terrain::Storage::VertexData& b)
    {
        int count = 0;
        for (std::uint32_t y = 0; y < a.normals->height(); ++y)
            for (std::uint32_t x = 0; x < a.normals->width(); ++x)
                for (int i = 0; i < 4; ++i)
                    if ((*a.normals)(x, y)[i] != (*b.normals)(x, y)[i])
                    {
                        ++count;
                        break;
                    }
        return count;
    }

    int differingColours(const Terrain::Storage::VertexData& a, const Terrain::Storage::VertexData& b)
    {
        int count = 0;
        for (std::uint32_t y = 0; y < a.colors->height(); ++y)
            for (std::uint32_t x = 0; x < a.colors->width(); ++x)
                for (int i = 0; i < 4; ++i)
                    if ((*a.colors)(x, y)[i] != (*b.colors)(x, y)[i])
                    {
                        ++count;
                        break;
                    }
        return count;
    }

    void expectColour(const vsg::ubvec4& colour, int r, int g, int b, int a)
    {
        EXPECT_EQ(static_cast<int>(colour[0]), r);
        EXPECT_EQ(static_cast<int>(colour[1]), g);
        EXPECT_EQ(static_cast<int>(colour[2]), b);
        EXPECT_EQ(static_cast<int>(colour[3]), a);
    }

    void expectNormal(const vsg::vec4& normal, float x, float y, float z)
    {
        constexpr float tolerance = 1e-4f;
        EXPECT_NEAR(normal[0], x, tolerance);
        EXPECT_NEAR(normal[1], y, tolerance);
        EXPECT_NEAR(normal[2], z, tolerance);
    }

    // ---- No alterations: the game's output ----

    TEST(ESMTerrainAlteredHeights, withHooksDoingNothingOutputEqualsTheUnalteredStorageExactly)
    {
        LandWorld world;
        world.addGradedSquare();
        PlainStorage plain(world);
        DefaultHooksStorage hooks(world);

        struct Case
        {
            int mLod;
            Terrain::Bounds mBounds;
        };
        const Case cases[] = {
            { 0, makeBounds(0, 0, 1) },
            { 0, makeBounds(0, 0, 2) },
            { 1, makeBounds(0, 0, 1) },
            { 2, makeBounds(0, 0, 2) },
            { 0, makeBounds(5, 5, 1) }, // no land at all
        };

        for (const Case& c : cases)
        {
            const auto expected = plain.getVertexData(c.mLod, c.mBounds);
            const auto actual = hooks.getVertexData(c.mLod, c.mBounds);
            EXPECT_EQ(differingHeights(expected, actual), 0);
            EXPECT_EQ(differingNormals(expected, actual), 0);
            EXPECT_EQ(differingColours(expected, actual), 0);
        }
    }

    TEST(ESMTerrainAlteredHeights, useAlterationFalseMeansTheHooksAreNeverConsulted)
    {
        // Old storage.cpp line 263: the game's storage returns false, and neither hook runs per vertex.
        LandWorld world;
        world.addGradedCell(0, 0);
        PlainStorage plain(world);
        AlteringStorage off(world, false);
        off.mHeightOffsets[{ 2, 3 }] = 1000.f;
        off.mRedOverrides[{ 2, 3 }] = 200;

        const auto expected = plain.getVertexData(0, makeBounds(0, 0, 1));
        const auto actual = off.getVertexData(0, makeBounds(0, 0, 1));

        EXPECT_EQ(off.mHeightCalls, 0);
        EXPECT_EQ(off.mColourCalls, 0);
        EXPECT_EQ(differingHeights(expected, actual), 0);
        EXPECT_EQ(differingColours(expected, actual), 0);
    }

    // ---- Height alteration at a vertex ----

    TEST(ESMTerrainAlteredHeights, offsetMovesOnlyThatVertexByThatAmount)
    {
        LandWorld world;
        world.addGradedCell(0, 0);
        PlainStorage plain(world);
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 3 }] = 7.5f; // col 2, row 3

        const auto base = plain.getVertexData(0, makeBounds(0, 0, 1));
        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // record height at col 2, row 3 = 0 + 2 * 65 + 3 = 133; 133 + 7.5 = 140.5. x = row = 3, y = col = 2.
        EXPECT_EQ((*output.heights)(3, 2), 140.5f);
        // The transposed vertex, col 3, row 2, must not move: 3 * 65 + 2 = 197.
        EXPECT_EQ((*output.heights)(2, 3), 197.f);
        EXPECT_EQ(differingHeights(base, output), 1);
    }

    TEST(ESMTerrainAlteredHeights, offsetIsAddedToTheRecordHeightNotAReplacement)
    {
        LandWorld world;
        world.addGradedCell(0, 0);
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 3 }] = -200.f;

        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // 133 - 200 = -67
        EXPECT_EQ((*output.heights)(3, 2), -67.f);
    }

    TEST(ESMTerrainAlteredHeights, offsetAppliesOnTopOfTheDefaultHeightWhereThereIsNoLand)
    {
        // Old storage.cpp lines 310-314: the alteration is outside the heightData check.
        LandWorld world;
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 3 }] = 16.f;
        altered.mRedOverrides[{ 2, 3 }] = 200;

        const auto output = altered.getVertexData(0, makeBounds(5, 5, 1));

        // DEFAULT_HEIGHT -2048 + 16 = -2032; other vertices stay at -2048.
        EXPECT_EQ((*output.heights)(3, 2), -2032.f);
        EXPECT_EQ((*output.heights)(4, 2), -2048.f);
        // adjustColor is told there is no height data, and starts from white.
        EXPECT_TRUE(altered.mHeightDataWasNull);
        expectColour((*output.colors)(3, 2), 200, 255, 255, 255);
    }

    TEST(ESMTerrainAlteredHeights, atLowerLodTheHookIsAskedInSourceCoordinates)
    {
        // Old storage.cpp line 314 passes col and row of the land record, not the output vertex.
        LandWorld world;
        world.addGradedCell(0, 0);
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 4 }] = 5.f; // sampled at LOD 1: both even
        altered.mHeightOffsets[{ 3, 3 }] = 5.f; // not sampled at LOD 1

        const auto output = altered.getVertexData(1, makeBounds(0, 0, 1));

        ASSERT_EQ(output.heights->width(), 33u); // 64 / 2 + 1
        // col 2, row 4 is output vertex x = 4 / 2 = 2, y = 2 / 2 = 1; 2 * 65 + 4 = 134; 134 + 5 = 139.
        EXPECT_EQ((*output.heights)(2, 1), 139.f);
        // col 4, row 4 is output vertex (2, 2), unaltered: 4 * 65 + 4 = 264.
        EXPECT_EQ((*output.heights)(2, 2), 264.f);
    }

    // ---- Normals and colours ----

    TEST(ESMTerrainAlteredHeights, normalsAreNotRecalculatedFromAlteredHeights)
    {
        // Old storage.cpp lines 324-344 read normals from the record only.
        LandWorld world;
        world.addGradedSquare();
        PlainStorage plain(world);
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 3 }] = 4000.f; // a spike no recalculation could ignore
        altered.mHeightOffsets[{ 2, 4 }] = -4000.f;

        const auto base = plain.getVertexData(0, makeBounds(0, 0, 1));
        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        EXPECT_EQ(differingNormals(base, output), 0);
        // Raw normal at col 2, row 3 = (3 * 10, 2 * 10, 100) = (30, 20, 100).
        // Length = sqrt(900 + 400 + 10000) = sqrt(11300) = 106.30146.
        // Normalized = (0.282216, 0.188144, 0.940721).
        expectNormal((*output.normals)(3, 2), 0.282216f, 0.188144f, 0.940721f);
    }

    TEST(ESMTerrainAlteredHeights, heightOffsetAloneLeavesColoursAlone)
    {
        LandWorld world;
        world.addGradedSquare();
        PlainStorage plain(world);
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 2, 3 }] = 4000.f;

        const auto base = plain.getVertexData(0, makeBounds(0, 0, 1));
        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        EXPECT_EQ(differingColours(base, output), 0);
    }

    TEST(ESMTerrainAlteredHeights, adjustColorChangesThatVertexAndIsGivenTheRecordColourAndHeightData)
    {
        LandWorld world;
        world.addGradedSquare();
        PlainStorage plain(world);
        AlteringStorage altered(world);
        altered.mRedOverrides[{ 2, 3 }] = 200;

        const auto base = plain.getVertexData(0, makeBounds(0, 0, 1));
        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // Record colour at col 2, row 3 in cell (0, 0) = (2, 3, 10), alpha 255 from storage.cpp's own default.
        ASSERT_TRUE(altered.mColourSeen.has_value());
        expectColour(*altered.mColourSeen, 2, 3, 10, 255);
        // heightData is the land record's, and holds the record height without the alteration: 133.
        ASSERT_TRUE(altered.mRecordHeightSeen.has_value());
        EXPECT_EQ(*altered.mRecordHeightSeen, 133.f);

        expectColour((*output.colors)(3, 2), 200, 3, 10, 255);
        EXPECT_EQ(differingColours(base, output), 1);
    }

    // ---- Cell edges ----

    TEST(ESMTerrainAlteredHeights, offsetOnCellEdgeAppliesToThatVertexOfThisStorageOutput)
    {
        // Old storage.cpp has no height equivalent of fixNormal or fixColour: an edge vertex's height is its own
        // cell's record height plus its own offset.
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 11, 64 }] = 8.f;

        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // col 11, row 64: 11 * 65 + 64 = 779; 779 + 8 = 787.
        EXPECT_EQ((*output.heights)(64, 11), 787.f);
    }

    TEST(ESMTerrainAlteredHeights, normalOnCellEdgeStillComesFromTheNeighboursRecord)
    {
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 11, 64 }] = 4000.f;

        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // Old storage.cpp lines 335-336: fixNormal reads cell (1, 0), col 11, row 0.
        // Raw = (0 * 10, 11 % 5 * 10, 100) = (0, 10, 100). Length = sqrt(10100) = 100.49876.
        // Normalized = (0, 0.099504, 0.995037). This cell's own raw x at row 64 would have been 40.
        expectNormal((*output.normals)(64, 11), 0.f, 0.099504f, 0.995037f);
    }

    TEST(ESMTerrainAlteredHeights, adjustColorOnCellEdgeIsOverwrittenByTheNeighboursRecord)
    {
        // Old storage.cpp lines 353-358: adjustColor, then fixColour.
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage altered(world);
        altered.mRedOverrides[{ 11, 64 }] = 200;
        altered.mRedOverrides[{ 64, 11 }] = 200;

        const auto output = altered.getVertexData(0, makeBounds(0, 0, 1));

        // Last row, col 11: fixColour reads cell (1, 0) at col 11, row 0 = (11, 0, 10 + 10) = (11, 0, 20).
        expectColour((*output.colors)(64, 11), 11, 0, 20, 255);
        // Last col, row 11: fixColour reads cell (0, 1) at col 0, row 11 = (0, 11, 10 + 20) = (0, 11, 30).
        expectColour((*output.colors)(11, 64), 0, 11, 30, 255);
    }

    TEST(ESMTerrainAlteredHeights, sharedBorderMatchesWhenBothCellsAlterItAlike)
    {
        // Flat land, so the border vertex has one height seen from either cell. Old storage.cpp does not reconcile
        // borders, so the two cells stay together only because the subclass gives both the same offset.
        LandWorld world;
        world.addFlatCell(0, 0, 100.f);
        world.addFlatCell(1, 0, 100.f);
        AlteringStorage left(world);
        left.mHeightOffsets[{ 10, 64 }] = 8.f;
        AlteringStorage right(world);
        right.mHeightOffsets[{ 10, 0 }] = 8.f;

        const auto leftOutput = left.getVertexData(0, makeBounds(0, 0, 1));
        const auto rightOutput = right.getVertexData(0, makeBounds(1, 0, 1));

        // 100 + 8 on both sides of the border.
        EXPECT_EQ((*leftOutput.heights)(64, 10), 108.f);
        EXPECT_EQ((*rightOutput.heights)(0, 10), 108.f);
    }

    TEST(ESMTerrainAlteredHeights, sharedBorderIsNotReconciledWhenOnlyOneCellAltersIt)
    {
        LandWorld world;
        world.addFlatCell(0, 0, 100.f);
        world.addFlatCell(1, 0, 100.f);
        AlteringStorage left(world);
        left.mHeightOffsets[{ 10, 64 }] = 8.f;
        PlainStorage right(world);

        const auto leftOutput = left.getVertexData(0, makeBounds(0, 0, 1));
        const auto rightOutput = right.getVertexData(0, makeBounds(1, 0, 1));

        EXPECT_EQ((*leftOutput.heights)(64, 10), 108.f);
        EXPECT_EQ((*rightOutput.heights)(0, 10), 100.f);
    }

    TEST(ESMTerrainAlteredHeights, inAMultiCellChunkTheBorderVertexIsTheFirstCellsLastRow)
    {
        // The vertex at x = 64 comes from cell (0, 0), row 64. Cell (1, 0)'s row 0 is skipped (storage.cpp's "Skip
        // the first row / column"), so an offset for row 0 reaches only the chunk's own first column.
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage last(world);
        last.mHeightOffsets[{ 10, 64 }] = 8.f;
        AlteringStorage first(world);
        first.mHeightOffsets[{ 10, 0 }] = 8.f;

        const auto lastOutput = last.getVertexData(0, makeBounds(0, 0, 2));
        const auto firstOutput = first.getVertexData(0, makeBounds(0, 0, 2));

        // col 10, row 64 of cell (0, 0): 10 * 65 + 64 = 714; 714 + 8 = 722.
        EXPECT_EQ((*lastOutput.heights)(64, 10), 722.f);
        // Offset for row 0: x = 64 stays at 714, while x = 0 moves: 10 * 65 + 0 + 8 = 658.
        EXPECT_EQ((*firstOutput.heights)(64, 10), 714.f);
        EXPECT_EQ((*firstOutput.heights)(0, 10), 658.f);
    }

    // ---- One cell's alterations and another cell ----

    TEST(ESMTerrainAlteredHeights, alterationsInOneStorageDoNotReachAnotherCellsStorageOrTheLandRecord)
    {
        // The hook has no cell argument, so a storage serves one cell's alterations (see the next test).
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage cellA(world);
        cellA.mHeightOffsets[{ 2, 3 }] = 7.5f;
        cellA.mRedOverrides[{ 2, 3 }] = 200;
        PlainStorage cellB(world);

        const auto before = cellB.getVertexData(0, makeBounds(1, 0, 1));
        const auto outputA = cellA.getVertexData(0, makeBounds(0, 0, 1));
        const auto after = cellB.getVertexData(0, makeBounds(1, 0, 1));

        EXPECT_EQ((*outputA.heights)(3, 2), 140.5f);
        EXPECT_EQ(differingHeights(before, after), 0);
        EXPECT_EQ(differingNormals(before, after), 0);
        EXPECT_EQ(differingColours(before, after), 0);

        // The land record itself is untouched: col 2, row 3 of cell (0, 0) is still 133.
        EXPECT_EQ(world.find(0, 0)->getLandData()->mHeights[2 * 65 + 3], 133.f);
    }

    TEST(ESMTerrainAlteredHeights, hookWithoutACellAppliesInEveryCellOfAMultiCellChunk)
    {
        // Pins old storage.cpp line 314: getAlteredHeight(col, row) is asked for every cell the chunk spans, with
        // nothing to say which. A caller wanting one cell altered must query that cell's bounds alone.
        LandWorld world;
        world.addGradedSquare();
        AlteringStorage altered(world);
        altered.mHeightOffsets[{ 10, 10 }] = 8.f;

        const auto output = altered.getVertexData(0, makeBounds(0, 0, 2));

        ASSERT_EQ(output.heights->width(), 129u); // 64 * 2 + 1
        // Record height at col 10, row 10 = cellBase + 660. In later cells row 0 is skipped, so row 10 is at
        // vertex 65 + (10 - 1) = 74.
        EXPECT_EQ((*output.heights)(10, 10), 668.f); // 0 + 660 + 8
        EXPECT_EQ((*output.heights)(74, 10), 10668.f); // 10000 + 660 + 8
        EXPECT_EQ((*output.heights)(10, 74), 20668.f); // 20000 + 660 + 8
        EXPECT_EQ((*output.heights)(74, 74), 30668.f); // 30000 + 660 + 8
    }

    // ---- getHeightAt ----

    TEST(ESMTerrainAlteredHeights, getHeightAtReadsTheRecordAndIgnoresAlterations)
    {
        // Old storage.cpp lines 568-650 never call getAlteredHeight.
        LandWorld world;
        world.addFlatCell(0, 0, 100.f);
        AlteringStorage altered(world);
        for (int col = 0; col < landSize; ++col)
            for (int row = 0; row < landSize; ++row)
                altered.mHeightOffsets[{ col, row }] = 8.f;

        const float height = altered.getHeightAt(vsg::vec3(1000.f, 1000.f, 0.f), ESM::RefId{});

        EXPECT_NEAR(height, 100.f, 0.01f);
    }
}
