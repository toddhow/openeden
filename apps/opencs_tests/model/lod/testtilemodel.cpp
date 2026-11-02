#include "apps/opencs/model/lod/tilemodel.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "tilefixtures.hpp"

namespace CSMLod
{
    namespace
    {
        using CSMLodTest::makeBaked;
        using CSMLodTest::makeManifest;
        using CSMLodTest::makeTile;
        using CSMLodTest::tileAt;

        /// Mixed levels, given out of key order: three level 0, two level 1 and one level 2 tile, with negative
        /// cells, and normals and albedo on some.
        std::vector<Lod::BakedTile> mixedTiles()
        {
            std::vector<Lod::BakedTile> tiles;
            struct Spec
            {
                std::uint32_t mLevel;
                std::int32_t mCellX, mCellY;
                bool mNormals, mAlbedo;
            };
            const Spec specs[] = {
                { 1, 2, 2, true, false },
                { 0, 0, 0, false, false },
                { 2, -4, 4, true, true },
                { 0, -1, -1, false, true },
                { 1, -2, 0, false, false },
                { 0, -1, 0, true, false },
            };
            float error = 1.f;
            for (const Spec& spec : specs)
            {
                const Lod::TileId id = tileAt(spec.mLevel, spec.mCellX, spec.mCellY);
                tiles.push_back(
                    makeBaked(id, makeTile(id, 3, -100.f + error, 500.f + error, error, spec.mNormals, spec.mAlbedo)));
                error += 1.5f;
            }
            return tiles;
        }

        const Lod::BakedTile* findSource(const std::vector<Lod::BakedTile>& tiles, const Lod::TileId& id)
        {
            for (const Lod::BakedTile& tile : tiles)
                if (tile.mId == id)
                    return &tile;
            return nullptr;
        }

        void expectSameTile(const Lod::TerrainTile& expected, const Lod::TerrainTile& actual)
        {
            EXPECT_EQ(expected.mGridSize, actual.mGridSize);
            EXPECT_EQ(expected.mLevel, actual.mLevel);
            EXPECT_EQ(expected.mSide, actual.mSide);
            EXPECT_EQ(expected.mOriginX, actual.mOriginX);
            EXPECT_EQ(expected.mOriginY, actual.mOriginY);
            EXPECT_EQ(expected.mMinHeight, actual.mMinHeight);
            EXPECT_EQ(expected.mMaxHeight, actual.mMaxHeight);
            EXPECT_EQ(expected.mGeometricError, actual.mGeometricError);
            EXPECT_EQ(expected.mHasNormals, actual.mHasNormals);
            EXPECT_EQ(expected.mHasAlbedo, actual.mHasAlbedo);
            EXPECT_EQ(expected.mHeights, actual.mHeights);
            EXPECT_EQ(expected.mNormals, actual.mNormals);
            EXPECT_EQ(expected.mAlbedo, actual.mAlbedo);
            EXPECT_EQ(expected.mAlbedoWidth, actual.mAlbedoWidth);
            EXPECT_EQ(expected.mAlbedoHeight, actual.mAlbedoHeight);
        }

        void expectRowsMatch(const TileModel& model, const std::vector<Lod::BakedTile>& tiles)
        {
            const std::vector<TileRow>& rows = model.rows();
            ASSERT_EQ(rows.size(), tiles.size());
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                const TileRow& row = rows[i];
                const Lod::BakedTile* source = findSource(tiles, row.mId);
                ASSERT_NE(source, nullptr) << "row " << i;
                const Lod::TerrainTile& data = source->mData;

                const std::optional<std::uint64_t> key = Lod::packTileKey(row.mId);
                ASSERT_TRUE(key.has_value());
                EXPECT_EQ(row.mKey, *key);
                EXPECT_EQ(row.mLevel, row.mId.mLevel);
                EXPECT_EQ(row.mTileX, row.mId.mX);
                EXPECT_EQ(row.mTileY, row.mId.mY);

                std::int32_t cellX = 0;
                std::int32_t cellY = 0;
                Lod::tileOriginCell(row.mId, cellX, cellY);
                EXPECT_EQ(row.mCellX, cellX);
                EXPECT_EQ(row.mCellY, cellY);

                EXPECT_EQ(row.mOriginX, data.mOriginX);
                EXPECT_EQ(row.mOriginY, data.mOriginY);
                EXPECT_EQ(row.mSide, data.mSide);
                EXPECT_EQ(row.mMinHeight, data.mMinHeight);
                EXPECT_EQ(row.mMaxHeight, data.mMaxHeight);
                EXPECT_EQ(row.mError, data.mGeometricError);
                EXPECT_EQ(row.mGridSize, data.mGridSize);

                std::uint16_t flags = 0;
                if (data.mHasNormals)
                    flags = static_cast<std::uint16_t>(flags | Lod::flagHasNormals);
                if (data.mHasAlbedo)
                    flags = static_cast<std::uint16_t>(flags | Lod::flagHasAlbedo);
                EXPECT_EQ(row.mFlags, flags);

                Lod::TerrainTileLayout layout;
                const Lod::LodError layoutError = Lod::computeTerrainTileLayout(data, layout);
                ASSERT_TRUE(layoutError.ok()) << layoutError.describe();
                EXPECT_EQ(row.mSizeBytes, layout.mTotalSize);
            }
        }

        TEST(CSMLodTileModelTest, opensAPackFromAPathAndListsRowsInKeyOrder)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            const Lod::TileManifest manifest = makeManifest(tiles, 3);
            CSMLodTest::TempFile file;
            Lod::TilePackOptions options;
            options.mStoreOnly = true;
            const Lod::LodError written = Lod::writeTilePack(manifest, tiles, options, file.path());
            ASSERT_TRUE(written.ok()) << written.describe();

            Lod::LodError error;
            const std::unique_ptr<TileModel> model = TileModel::open(file.path(), error);
            ASSERT_NE(model, nullptr) << error.describe();
            EXPECT_TRUE(error.ok());

            const std::vector<TileRow>& rows = model->rows();
            ASSERT_EQ(rows.size(), tiles.size());
            for (std::size_t i = 1; i < rows.size(); ++i)
                EXPECT_LT(rows[i - 1].mKey, rows[i].mKey) << "row " << i;
            const std::vector<Lod::TileId>& ids = model->pack().tileIds();
            ASSERT_EQ(ids.size(), rows.size());
            for (std::size_t i = 0; i < rows.size(); ++i)
                EXPECT_EQ(ids[i], rows[i].mId);

            expectRowsMatch(*model, tiles);
        }

        TEST(CSMLodTileModelTest, adoptsAnOpenPackWithTheSameRows)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = CSMLodTest::makeModel(makeManifest(tiles, 3), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();
            EXPECT_TRUE(error.ok());
            expectRowsMatch(*model, tiles);
            EXPECT_EQ(model->pack().manifest().mGridSize, 3u);
        }

        TEST(CSMLodTileModelTest, rowsOfAMixedLevelPackKeepEachTilesOwnLevelAndCell)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = CSMLodTest::makeModel(makeManifest(tiles, 3), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();

            std::size_t levelCounts[3] = { 0, 0, 0 };
            for (const TileRow& row : model->rows())
            {
                ASSERT_LT(row.mLevel, 3u);
                ++levelCounts[row.mLevel];
                EXPECT_EQ(row.mSide, static_cast<float>(CSMLodTest::cellSize) * static_cast<float>(1u << row.mLevel));
            }
            EXPECT_EQ(levelCounts[0], 3u);
            EXPECT_EQ(levelCounts[1], 2u);
            EXPECT_EQ(levelCounts[2], 1u);

            // The level 2 tile at cell (-4, 4) has its south-west cell there (negative cells are not biased away).
            bool found = false;
            for (const TileRow& row : model->rows())
            {
                if (row.mLevel == 2)
                {
                    found = true;
                    EXPECT_EQ(row.mCellX, -4);
                    EXPECT_EQ(row.mCellY, 4);
                    EXPECT_EQ(row.mFlags, static_cast<std::uint16_t>(Lod::flagHasNormals | Lod::flagHasAlbedo));
                }
            }
            EXPECT_TRUE(found);
        }

        TEST(CSMLodTileModelTest, readTileReturnsTheWholeTileOfEachRow)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = CSMLodTest::makeModel(makeManifest(tiles, 3), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();

            for (std::size_t i = 0; i < model->rows().size(); ++i)
            {
                Lod::TerrainTile tile;
                const Lod::LodError readError = model->readTile(i, tile);
                ASSERT_TRUE(readError.ok()) << "row " << i << ": " << readError.describe();
                const Lod::BakedTile* source = findSource(tiles, model->rows()[i].mId);
                ASSERT_NE(source, nullptr);
                expectSameTile(source->mData, tile);
            }
        }

        TEST(CSMLodTileModelTest, cellSizeComesFromTheFirstWorldspace)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = CSMLodTest::makeModel(makeManifest(tiles, 3, 4096), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();
            EXPECT_EQ(model->cellSize(), 4096u);
        }

        TEST(CSMLodTileModelTest, cellSizeIs8192WhenTheManifestHasNoWorldspace)
        {
            const std::vector<Lod::BakedTile> tiles = mixedTiles();
            Lod::LodError error;
            const std::unique_ptr<TileModel> model
                = CSMLodTest::makeModel(makeManifest(tiles, 3, CSMLodTest::cellSize, false), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();
            EXPECT_EQ(model->cellSize(), 8192u);
        }

        TEST(CSMLodTileModelTest, flagsReportNormalsAndAlbedoSeparately)
        {
            std::vector<Lod::BakedTile> tiles;
            const Lod::TileId none = tileAt(0, 0, 0);
            const Lod::TileId normals = tileAt(0, 1, 0);
            const Lod::TileId albedo = tileAt(0, 2, 0);
            const Lod::TileId both = tileAt(0, 3, 0);
            tiles.push_back(makeBaked(none, makeTile(none, 3, 0.f, 10.f, 1.f, false, false)));
            tiles.push_back(makeBaked(normals, makeTile(normals, 3, 0.f, 10.f, 1.f, true, false)));
            tiles.push_back(makeBaked(albedo, makeTile(albedo, 3, 0.f, 10.f, 1.f, false, true)));
            tiles.push_back(makeBaked(both, makeTile(both, 3, 0.f, 10.f, 1.f, true, true)));
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = CSMLodTest::makeModel(makeManifest(tiles, 3), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();
            ASSERT_EQ(model->rows().size(), 4u);
            for (const TileRow& row : model->rows())
            {
                if (row.mId == none)
                {
                    EXPECT_EQ(row.mFlags, 0u);
                }
                else if (row.mId == normals)
                {
                    EXPECT_EQ(row.mFlags, Lod::flagHasNormals);
                }
                else if (row.mId == albedo)
                {
                    EXPECT_EQ(row.mFlags, Lod::flagHasAlbedo);
                }
                else
                {
                    EXPECT_EQ(row.mFlags, static_cast<std::uint16_t>(Lod::flagHasNormals | Lod::flagHasAlbedo));
                }
            }
        }

        TEST(CSMLodTileModelTest, openFailsWithAnErrorWhenThePathDoesNotExist)
        {
            CSMLodTest::TempFile file; // never created
            Lod::LodError error;
            const std::unique_ptr<TileModel> model = TileModel::open(file.path(), error);
            EXPECT_EQ(model, nullptr);
            EXPECT_FALSE(error.ok());
        }
    }
}
