#include "apps/opencs/model/lod/tilemodel.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tilefixtures.hpp"

namespace CSMLod
{
    namespace
    {
        constexpr double halfPi = 1.5707963267948966;
        constexpr double cellSizeD = 8192.0;

        using Cells = std::vector<std::pair<std::int32_t, std::int32_t>>;

        std::unique_ptr<TileModel> makeFlatModel(const Cells& cells, float error)
        {
            const std::vector<Lod::BakedTile> tiles = CSMLodTest::makeFlatLevelOneTiles(cells, error);
            Lod::LodError buildError;
            std::unique_ptr<TileModel> model
                = CSMLodTest::makeModel(CSMLodTest::makeManifest(tiles, 3), tiles, buildError);
            EXPECT_NE(model, nullptr) << buildError.describe();
            return model;
        }

        /// A camera that looks north (+Y) with a 90 degree vertical field of view, so that a tile's error in pixels
        /// is error * viewport / (2 * distance), and a view distance that skips nothing unless the test says so.
        WorldGridOptions northOptions(double x, double y, double z)
        {
            WorldGridOptions options;
            options.mCamera.mX = x;
            options.mCamera.mY = y;
            options.mCamera.mZ = z;
            options.mCamera.mForwardX = 0;
            options.mCamera.mForwardY = 1;
            options.mCamera.mForwardZ = 0;
            options.mCamera.mViewportHeight = 1000;
            options.mCamera.mFovY = halfPi;
            options.mCamera.mViewDistance = 1e9;
            options.mThreshold = 4.f;
            options.mScale = 2;
            return options;
        }

        /// Finds the row of the tile whose south-west cell is (cellX, cellY); the row count when there is none.
        std::size_t rowOfCell(const TileModel& model, std::int32_t cellX, std::int32_t cellY)
        {
            const std::vector<TileRow>& rows = model.rows();
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (rows[i].mCellX == cellX && rows[i].mCellY == cellY)
                    return i;
            return rows.size();
        }

        Rgba pixelAt(const RgbaImage& image, std::uint32_t x, std::uint32_t y)
        {
            const std::size_t at = (static_cast<std::size_t>(y) * image.mWidth + x) * 4;
            return Rgba{ image.mPixels[at], image.mPixels[at + 1], image.mPixels[at + 2], image.mPixels[at + 3] };
        }

        Rgba colourOf(TileDraw draw)
        {
            switch (draw)
            {
                case TileDraw::Tile:
                    return mapTile;
                case TileDraw::Live:
                    return mapLive;
                case TileDraw::Skipped:
                    break;
            }
            return mapSkipped;
        }

        /// Checks the interior of the tile square at cells [cellX, cellX + 2) x [cellY, cellY + 2): the map's rows
        /// run north to south, and the outermost pixel ring of a square is not promised.
        void expectSquareInterior(const WorldGridResult& result, std::int32_t totalCellsY, std::uint32_t scale,
            std::int32_t cellX, std::int32_t cellY, const Rgba& expected)
        {
            const std::uint32_t side = 2 * scale;
            const std::uint32_t left = static_cast<std::uint32_t>(cellX - result.mMinCellX) * scale;
            const std::uint32_t top = static_cast<std::uint32_t>(totalCellsY - (cellY - result.mMinCellY) - 2) * scale;
            ASSERT_LE(left + side, result.mMap.mWidth);
            ASSERT_LE(top + side, result.mMap.mHeight);
            for (std::uint32_t y = top + 1; y + 1 < top + side; ++y)
                for (std::uint32_t x = left + 1; x + 1 < left + side; ++x)
                    EXPECT_EQ(pixelAt(result.mMap, x, y), expected) << "pixel " << x << ", " << y;
        }

        // Three by three level 1 tiles: south-west cells 0, 2 and 4 in both directions (a 6 x 6 cell map), each
        // 16384 world units wide.
        Cells threeByThree()
        {
            Cells cells;
            for (std::int32_t y = 0; y < 6; y += 2)
                for (std::int32_t x = 0; x < 6; x += 2)
                    cells.emplace_back(x, y);
            return cells;
        }

        TEST(CSMLodWorldGridTest, aNearTileIsLiveAndTheSameTileFarAwayIsATile)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);

            // Pixels = 100 * 1000 / (2 * d): 50 at d = 1000, 0.5 at d = 100000 (the tile's south edge is y = 0).
            {
                WorldGridView view(*model);
                const WorldGridResult result = view.update(northOptions(8192, -1000, 0));
                ASSERT_EQ(result.mTiles.size(), 1u);
                EXPECT_EQ(result.mTiles[0].mDraw, TileDraw::Live);
                EXPECT_NEAR(result.mTiles[0].mPixels, 50.f, 0.5f);
                EXPECT_EQ(result.mLive, 1u);
                EXPECT_EQ(result.mDrawn, 0u);
                EXPECT_EQ(result.mSkipped, 0u);
            }
            {
                WorldGridView view(*model);
                const WorldGridResult result = view.update(northOptions(8192, -100000, 0));
                ASSERT_EQ(result.mTiles.size(), 1u);
                EXPECT_EQ(result.mTiles[0].mDraw, TileDraw::Tile);
                EXPECT_NEAR(result.mTiles[0].mPixels, 0.5f, 0.01f);
                EXPECT_EQ(result.mDrawn, 1u);
                EXPECT_EQ(result.mLive, 0u);
            }
        }

        TEST(CSMLodWorldGridTest, aTileBeyondTheViewDistanceIsSkippedWithNoPixels)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridOptions options = northOptions(8192, -100000, 0);
            options.mCamera.mViewDistance = 50000;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            ASSERT_EQ(result.mTiles.size(), 1u);
            EXPECT_EQ(result.mTiles[0].mDraw, TileDraw::Skipped);
            EXPECT_EQ(result.mTiles[0].mPixels, 0.f);
            EXPECT_EQ(result.mSkipped, 1u);
            EXPECT_EQ(result.mDrawn + result.mLive, 0u);
        }

        TEST(CSMLodWorldGridTest, reportsOneEntryPerRowAndTheCountsAddUp)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);
            ASSERT_EQ(model->rows().size(), 9u);

            WorldGridOptions options = northOptions(24576, -5000, 0);
            options.mCamera.mViewDistance = 30000;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            ASSERT_EQ(result.mTiles.size(), model->rows().size());
            for (std::size_t i = 0; i < result.mTiles.size(); ++i)
                EXPECT_EQ(result.mTiles[i].mRow, i);
            EXPECT_EQ(result.mDrawn + result.mLive + result.mSkipped, model->rows().size());

            std::size_t drawn = 0;
            std::size_t live = 0;
            std::size_t skipped = 0;
            for (const WorldGridTile& tile : result.mTiles)
            {
                switch (tile.mDraw)
                {
                    case TileDraw::Tile:
                        ++drawn;
                        break;
                    case TileDraw::Live:
                        ++live;
                        break;
                    case TileDraw::Skipped:
                        ++skipped;
                        break;
                }
            }
            EXPECT_EQ(drawn, result.mDrawn);
            EXPECT_EQ(live, result.mLive);
            EXPECT_EQ(skipped, result.mSkipped);

            // The middle column, straight ahead: the nearest row is 5000 units away (10 pixels, live), the next
            // 21384 away (2.3 pixels, a tile), and the far row, 37768 away, is beyond the view distance.
            EXPECT_EQ(result.mTiles[rowOfCell(*model, 2, 0)].mDraw, TileDraw::Live);
            EXPECT_EQ(result.mTiles[rowOfCell(*model, 2, 2)].mDraw, TileDraw::Tile);
            EXPECT_EQ(result.mTiles[rowOfCell(*model, 2, 4)].mDraw, TileDraw::Skipped);
            for (const WorldGridTile& tile : result.mTiles)
                if (tile.mDraw == TileDraw::Skipped)
                {
                    EXPECT_EQ(tile.mPixels, 0.f);
                }
        }

        TEST(CSMLodWorldGridTest, forcedModesOverrideOnlyTheVisibleTiles)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);

            WorldGridOptions options = northOptions(24576, -5000, 0);
            options.mCamera.mViewDistance = 30000;
            WorldGridView autoView(*model);
            const WorldGridResult automatic = autoView.update(options);
            ASSERT_GT(automatic.mSkipped, 0u);
            ASSERT_LT(automatic.mSkipped, model->rows().size());

            const std::size_t visible = model->rows().size() - automatic.mSkipped;

            options.mMode = ForcedMode::AllTile;
            WorldGridView tileView(*model);
            const WorldGridResult allTile = tileView.update(options);
            options.mMode = ForcedMode::AllLive;
            WorldGridView liveView(*model);
            const WorldGridResult allLive = liveView.update(options);

            ASSERT_EQ(allTile.mTiles.size(), automatic.mTiles.size());
            ASSERT_EQ(allLive.mTiles.size(), automatic.mTiles.size());
            for (std::size_t i = 0; i < automatic.mTiles.size(); ++i)
            {
                if (automatic.mTiles[i].mDraw == TileDraw::Skipped)
                {
                    EXPECT_EQ(allTile.mTiles[i].mDraw, TileDraw::Skipped) << "row " << i;
                    EXPECT_EQ(allLive.mTiles[i].mDraw, TileDraw::Skipped) << "row " << i;
                }
                else
                {
                    EXPECT_EQ(allTile.mTiles[i].mDraw, TileDraw::Tile) << "row " << i;
                    EXPECT_EQ(allLive.mTiles[i].mDraw, TileDraw::Live) << "row " << i;
                }
            }
            EXPECT_EQ(allTile.mDrawn, visible);
            EXPECT_EQ(allTile.mLive, 0u);
            EXPECT_EQ(allTile.mSkipped, automatic.mSkipped);
            EXPECT_EQ(allLive.mLive, visible);
            EXPECT_EQ(allLive.mDrawn, 0u);
            EXPECT_EQ(allLive.mSkipped, automatic.mSkipped);
        }

        TEST(CSMLodWorldGridTest, theMapCoversTheBoundingBoxOfAllTilesTimesTheScale)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { -4, 2 }, { 0, 6 } }, 100.f);
            ASSERT_NE(model, nullptr);
            // Cells x from -4 to 1 (6 cells) and y from 2 to 7 (6 cells).
            for (const std::uint32_t scale : { 1u, 3u, 4u })
            {
                WorldGridOptions options = northOptions(0, -50000, 0);
                options.mScale = scale;
                WorldGridView view(*model);
                const WorldGridResult result = view.update(options);
                EXPECT_EQ(result.mMap.mWidth, 6 * scale) << "scale " << scale;
                EXPECT_EQ(result.mMap.mHeight, 6 * scale) << "scale " << scale;
                EXPECT_EQ(result.mMap.mPixels.size(), static_cast<std::size_t>(6 * scale) * 6 * scale * 4);
                EXPECT_EQ(result.mMinCellX, -4);
                EXPECT_EQ(result.mMinCellY, 2);
            }
        }

        TEST(CSMLodWorldGridTest, aScaleOfZeroIsTreatedAsOne)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridOptions options = northOptions(24576, -5000, 0);
            options.mScale = 0;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            EXPECT_EQ(result.mMap.mWidth, 6u);
            EXPECT_EQ(result.mMap.mHeight, 6u);
        }

        TEST(CSMLodWorldGridTest, tileSquaresAreFilledWithTheColourOfTheirState)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridOptions options = northOptions(24576, -5000, 0); // outside the map: no camera cross on top
            options.mCamera.mViewDistance = 30000;
            options.mScale = 4;

            for (const ForcedMode mode : { ForcedMode::Auto, ForcedMode::AllTile, ForcedMode::AllLive })
            {
                options.mMode = mode;
                WorldGridView view(*model);
                const WorldGridResult result = view.update(options);
                ASSERT_EQ(result.mMap.mWidth, 24u);
                ASSERT_EQ(result.mMap.mHeight, 24u);
                for (const WorldGridTile& tile : result.mTiles)
                {
                    const TileRow& row = model->rows()[tile.mRow];
                    expectSquareInterior(result, 6, options.mScale, row.mCellX, row.mCellY, colourOf(tile.mDraw));
                }
            }
        }

        TEST(CSMLodWorldGridTest, northIsAtTheTopOfTheMap)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 }, { 0, 4 } }, 100.f);
            ASSERT_NE(model, nullptr);
            // The camera is south of both tiles and looks north; the near one is live, the far one is a tile.
            WorldGridOptions options = northOptions(8192, -1000, 0);
            options.mScale = 4;
            options.mThreshold = 4.f;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            ASSERT_EQ(result.mTiles.size(), 2u);
            const TileDraw southDraw = result.mTiles[rowOfCell(*model, 0, 0)].mDraw;
            const TileDraw northDraw = result.mTiles[rowOfCell(*model, 0, 4)].mDraw;
            // 6 cells tall at scale 4: the tile at cell y 4 is the top square, the one at y 0 the bottom square.
            ASSERT_EQ(result.mMap.mHeight, 24u);
            EXPECT_EQ(pixelAt(result.mMap, 4, 4), colourOf(northDraw));
            EXPECT_EQ(pixelAt(result.mMap, 4, 20), colourOf(southDraw));
        }

        TEST(CSMLodWorldGridTest, whereNoTileLiesTheMapShowsTheBackground)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 }, { 4, 4 } }, 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridOptions options = northOptions(0, -50000, 0);
            options.mScale = 4;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            ASSERT_EQ(result.mMap.mWidth, 24u);
            ASSERT_EQ(result.mMap.mHeight, 24u);

            // Cells (2..3, 2..3) hold no tile: their middle is rows 8..15 and columns 8..15 of the map.
            for (std::uint32_t y = 9; y < 15; ++y)
                for (std::uint32_t x = 9; x < 15; ++x)
                    EXPECT_EQ(pixelAt(result.mMap, x, y), mapBackground) << "pixel " << x << ", " << y;
            // So do the south-east and the north-west squares.
            EXPECT_EQ(pixelAt(result.mMap, 20, 20), mapBackground);
            EXPECT_EQ(pixelAt(result.mMap, 4, 4), mapBackground);

            for (const WorldGridTile& tile : result.mTiles)
            {
                const TileRow& row = model->rows()[tile.mRow];
                expectSquareInterior(result, 6, options.mScale, row.mCellX, row.mCellY, colourOf(tile.mDraw));
            }
        }

        TEST(CSMLodWorldGridTest, theCameraIsDrawnOnTheMapAndItsPixelIsReported)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);
            // World (24576, 24576) is cell (3, 3): 3 cells from the west and 3 cells below the north edge of the
            // 6 x 6 cell map, so pixel (12, 12) at scale 4.
            WorldGridOptions options = northOptions(3 * cellSizeD, 3 * cellSizeD, 0);
            options.mScale = 4;
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            ASSERT_GE(result.mCameraPixelX, 0);
            ASSERT_GE(result.mCameraPixelY, 0);
            EXPECT_NEAR(result.mCameraPixelX, 12, 1);
            EXPECT_NEAR(result.mCameraPixelY, 12, 1);
            ASSERT_LT(static_cast<std::uint32_t>(result.mCameraPixelX), result.mMap.mWidth);
            ASSERT_LT(static_cast<std::uint32_t>(result.mCameraPixelY), result.mMap.mHeight);
            EXPECT_EQ(pixelAt(result.mMap, static_cast<std::uint32_t>(result.mCameraPixelX),
                          static_cast<std::uint32_t>(result.mCameraPixelY)),
                mapCamera);
        }

        TEST(CSMLodWorldGridTest, theCameraPixelIsMinusOneOutsideTheMap)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel(threeByThree(), 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridView view(*model);
            const WorldGridResult result = view.update(northOptions(24576, -5000, 0));
            EXPECT_EQ(result.mCameraPixelX, -1);
            EXPECT_EQ(result.mCameraPixelY, -1);
            for (std::size_t i = 0; i + 3 < result.mMap.mPixels.size(); i += 4)
                EXPECT_NE((Rgba{ result.mMap.mPixels[i], result.mMap.mPixels[i + 1], result.mMap.mPixels[i + 2],
                              result.mMap.mPixels[i + 3] }),
                    mapCamera);
        }

        TEST(CSMLodWorldGridTest, aLiveTileStaysLiveBetweenTheHysteresisShareAndTheThreshold)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);
            // 100 * 1000 / (2 * d): 50 pixels at 1000, 3.57 at 14000 (between 0.7 * 4 = 2.8 and 4), 0.5 at 100000.
            const WorldGridOptions nearOptions = northOptions(8192, -1000, 0);
            const WorldGridOptions betweenOptions = northOptions(8192, -14000, 0);
            const WorldGridOptions farOptions = northOptions(8192, -100000, 0);

            WorldGridView view(*model);
            ASSERT_EQ(view.update(nearOptions).mTiles[0].mDraw, TileDraw::Live);
            for (int call = 0; call < 4; ++call)
            {
                const WorldGridResult result = view.update(betweenOptions);
                ASSERT_EQ(result.mTiles.size(), 1u);
                EXPECT_EQ(result.mTiles[0].mDraw, TileDraw::Live) << "call " << call;
                EXPECT_NEAR(result.mTiles[0].mPixels, 3.57f, 0.02f) << "call " << call;
            }

            // A view with no history picks the tile at the same camera.
            WorldGridView fresh(*model);
            EXPECT_EQ(fresh.update(betweenOptions).mTiles[0].mDraw, TileDraw::Tile);

            // Far enough below 0.7 * threshold, the live tile returns to a tile, and then stays one.
            EXPECT_EQ(view.update(farOptions).mTiles[0].mDraw, TileDraw::Tile);
            EXPECT_EQ(view.update(betweenOptions).mTiles[0].mDraw, TileDraw::Tile);
        }

        TEST(CSMLodWorldGridTest, forcedModesNeverChangeTheStoredHysteresis)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);
            const WorldGridOptions nearOptions = northOptions(8192, -1000, 0);
            const WorldGridOptions betweenOptions = northOptions(8192, -14000, 0);

            // A view that was forced to Tile near the tile, then looked in Auto, equals one that was only ever Auto.
            WorldGridView forced(*model);
            WorldGridOptions forcedOptions = nearOptions;
            forcedOptions.mMode = ForcedMode::AllTile;
            EXPECT_EQ(forced.update(forcedOptions).mTiles[0].mDraw, TileDraw::Tile);
            const WorldGridResult afterForced = forced.update(betweenOptions);

            WorldGridView plain(*model);
            plain.update(nearOptions);
            const WorldGridResult afterPlain = plain.update(betweenOptions);

            ASSERT_EQ(afterForced.mTiles.size(), afterPlain.mTiles.size());
            EXPECT_EQ(afterForced.mTiles[0].mDraw, TileDraw::Live);
            EXPECT_EQ(afterForced.mTiles[0].mDraw, afterPlain.mTiles[0].mDraw);
            EXPECT_EQ(afterForced.mMap.mPixels, afterPlain.mMap.mPixels);
        }

        TEST(CSMLodWorldGridTest, aMapTooWideForTheLimitHasItsScaleReduced)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { 0, 0 }, { 1000, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridOptions options = northOptions(0, -50000, 0);
            options.mScale = 100; // 1002 cells * 100 would be far above the limit
            WorldGridView view(*model);
            const WorldGridResult result = view.update(options);
            EXPECT_EQ(result.mScale, 4u); // 4096 / 1002
            EXPECT_EQ(result.mMap.mWidth, 1002u * 4u);
            EXPECT_LE(result.mMap.mWidth, static_cast<std::uint32_t>(mapMaxSide));
            EXPECT_LE(result.mMap.mHeight, static_cast<std::uint32_t>(mapMaxSide));
            EXPECT_EQ(
                result.mMap.mPixels.size(), static_cast<std::size_t>(result.mMap.mWidth) * result.mMap.mHeight * 4);
        }

        TEST(CSMLodWorldGridTest, aDamagedPackWithFarApartTilesHasNoMap)
        {
            const std::unique_ptr<TileModel> model = makeFlatModel({ { -3000, 0 }, { 3000, 0 } }, 100.f);
            ASSERT_NE(model, nullptr);
            WorldGridView view(*model);
            const WorldGridResult result = view.update(northOptions(0, -50000, 0));
            EXPECT_TRUE(result.mMap.empty());
            EXPECT_EQ(result.mScale, 0u);
            EXPECT_EQ(result.mCameraPixelX, -1);
            EXPECT_EQ(result.mTiles.size(), 2u); // the states are still reported
        }

        TEST(CSMLodWorldGridTest, aHugeTileHasNoMap)
        {
            // Level 15 is the highest level a pack can hold: 32768 cells a side, far above the map limit.
            const Lod::TileId id = CSMLodTest::tileAt(15, 0, 0);
            std::vector<Lod::BakedTile> tiles;
            tiles.push_back(CSMLodTest::makeBaked(id, CSMLodTest::makeFlatTile(id, 3, 100.f)));
            Lod::LodError error;
            const std::unique_ptr<TileModel> model
                = CSMLodTest::makeModel(CSMLodTest::makeManifest(tiles, 3), tiles, error);
            ASSERT_NE(model, nullptr) << error.describe();
            WorldGridView view(*model);
            WorldGridOptions options = northOptions(0, 0, 0);
            options.mScale = 4000000000u;
            const WorldGridResult result = view.update(options);
            EXPECT_TRUE(result.mMap.empty());
            EXPECT_EQ(result.mTiles.size(), 1u);
        }

        TEST(CSMLodWorldGridTest, summariseListsTheCounts)
        {
            WorldGridResult result;
            result.mTiles.resize(6);
            result.mDrawn = 3;
            result.mLive = 2;
            result.mSkipped = 1;
            EXPECT_EQ(summarise(result), "tiles 6, drawn 3, live 2, skipped 1");

            const WorldGridResult empty;
            EXPECT_EQ(summarise(empty), "tiles 0, drawn 0, live 0, skipped 0");
        }

        TEST(CSMLodParseCameraSpecTest, readsAFullSpec)
        {
            WorldGridOptions options;
            const std::string message = parseCameraSpec("100,200,300,90,60,1080,2.5,live", options);
            EXPECT_TRUE(message.empty()) << message;
            EXPECT_DOUBLE_EQ(options.mCamera.mX, 100.0);
            EXPECT_DOUBLE_EQ(options.mCamera.mY, 200.0);
            EXPECT_DOUBLE_EQ(options.mCamera.mZ, 300.0);
            EXPECT_NEAR(options.mCamera.mForwardX, 1.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mForwardY, 0.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mForwardZ, 0.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mFovY, 60.0 * halfPi / 90.0, 1e-6);
            EXPECT_DOUBLE_EQ(options.mCamera.mViewportHeight, 1080.0);
            EXPECT_FLOAT_EQ(options.mThreshold, 2.5f);
            EXPECT_EQ(options.mMode, ForcedMode::AllLive);
        }

        TEST(CSMLodParseCameraSpecTest, modeNamesAreAutoTileAndLive)
        {
            WorldGridOptions options;
            EXPECT_TRUE(parseCameraSpec("0,0,0,0,50,720,4,tile", options).empty());
            EXPECT_EQ(options.mMode, ForcedMode::AllTile);
            EXPECT_TRUE(parseCameraSpec("0,0,0,0,50,720,4,live", options).empty());
            EXPECT_EQ(options.mMode, ForcedMode::AllLive);
            EXPECT_TRUE(parseCameraSpec("0,0,0,0,50,720,4,auto", options).empty());
            EXPECT_EQ(options.mMode, ForcedMode::Auto);
        }

        TEST(CSMLodParseCameraSpecTest, aPartialSpecKeepsTheOtherFields)
        {
            WorldGridOptions options;
            options.mCamera.mX = 1;
            options.mCamera.mY = 2;
            options.mCamera.mZ = 5;
            options.mCamera.mForwardX = 0.6;
            options.mCamera.mForwardY = 0.8;
            options.mCamera.mFovY = 0.75;
            options.mCamera.mViewportHeight = 480;
            options.mThreshold = 7.f;
            options.mMode = ForcedMode::AllTile;

            const std::string message = parseCameraSpec("10,20", options);
            EXPECT_TRUE(message.empty()) << message;
            EXPECT_DOUBLE_EQ(options.mCamera.mX, 10.0);
            EXPECT_DOUBLE_EQ(options.mCamera.mY, 20.0);
            EXPECT_DOUBLE_EQ(options.mCamera.mZ, 5.0);
            EXPECT_DOUBLE_EQ(options.mCamera.mForwardX, 0.6);
            EXPECT_DOUBLE_EQ(options.mCamera.mForwardY, 0.8);
            EXPECT_DOUBLE_EQ(options.mCamera.mFovY, 0.75);
            EXPECT_DOUBLE_EQ(options.mCamera.mViewportHeight, 480.0);
            EXPECT_FLOAT_EQ(options.mThreshold, 7.f);
            EXPECT_EQ(options.mMode, ForcedMode::AllTile);
        }

        TEST(CSMLodParseCameraSpecTest, headingZeroLooksNorthAndNinetyLooksEast)
        {
            WorldGridOptions options;
            ASSERT_TRUE(parseCameraSpec("0,0,0,0", options).empty());
            EXPECT_NEAR(options.mCamera.mForwardX, 0.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mForwardY, 1.0, 1e-9);
            ASSERT_TRUE(parseCameraSpec("0,0,0,90", options).empty());
            EXPECT_NEAR(options.mCamera.mForwardX, 1.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mForwardY, 0.0, 1e-9);
            ASSERT_TRUE(parseCameraSpec("0,0,0,180", options).empty());
            EXPECT_NEAR(options.mCamera.mForwardX, 0.0, 1e-9);
            EXPECT_NEAR(options.mCamera.mForwardY, -1.0, 1e-9);
        }

        void expectUnchanged(const WorldGridOptions& expected, const WorldGridOptions& actual)
        {
            EXPECT_DOUBLE_EQ(expected.mCamera.mX, actual.mCamera.mX);
            EXPECT_DOUBLE_EQ(expected.mCamera.mY, actual.mCamera.mY);
            EXPECT_DOUBLE_EQ(expected.mCamera.mZ, actual.mCamera.mZ);
            EXPECT_DOUBLE_EQ(expected.mCamera.mForwardX, actual.mCamera.mForwardX);
            EXPECT_DOUBLE_EQ(expected.mCamera.mForwardY, actual.mCamera.mForwardY);
            EXPECT_DOUBLE_EQ(expected.mCamera.mForwardZ, actual.mCamera.mForwardZ);
            EXPECT_DOUBLE_EQ(expected.mCamera.mFovY, actual.mCamera.mFovY);
            EXPECT_DOUBLE_EQ(expected.mCamera.mViewportHeight, actual.mCamera.mViewportHeight);
            EXPECT_FLOAT_EQ(expected.mThreshold, actual.mThreshold);
            EXPECT_EQ(expected.mMode, actual.mMode);
            EXPECT_EQ(expected.mScale, actual.mScale);
        }

        TEST(CSMLodParseCameraSpecTest, aBadNumberReturnsAMessageAndLeavesTheOptionsAlone)
        {
            WorldGridOptions options;
            options.mCamera.mX = 11;
            options.mCamera.mY = 22;
            options.mThreshold = 6.f;
            options.mMode = ForcedMode::AllTile;
            const WorldGridOptions before = options;

            // The bad field is the third; the valid ones before it must not be applied either.
            EXPECT_FALSE(parseCameraSpec("1,2,abc", options).empty());
            expectUnchanged(before, options);
            EXPECT_FALSE(parseCameraSpec("1,2,3,4,5,6,notanumber,live", options).empty());
            expectUnchanged(before, options);
        }

        TEST(CSMLodParseCameraSpecTest, aBadModeReturnsAMessageAndLeavesTheOptionsAlone)
        {
            WorldGridOptions options;
            options.mCamera.mX = 11;
            options.mThreshold = 6.f;
            options.mMode = ForcedMode::AllLive;
            const WorldGridOptions before = options;
            EXPECT_FALSE(parseCameraSpec("1,2,3,4,5,6,7,sideways", options).empty());
            expectUnchanged(before, options);
        }
    }
}
