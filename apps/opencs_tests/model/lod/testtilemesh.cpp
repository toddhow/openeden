#include "apps/opencs/model/lod/tilemodel.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "tilefixtures.hpp"

namespace CSMLod
{
    namespace
    {
        using Vec3 = std::array<float, 3>;

        // A tile with a known origin, side and height range; deterministic non-flat heights from the fixture.
        Lod::TerrainTile meshTile(std::uint32_t n, bool normals)
        {
            Lod::TerrainTile tile
                = CSMLodTest::makeTile(CSMLodTest::tileAt(0, 0, 0), n, -50.f, 150.f, 3.f, normals, false);
            tile.mOriginX = 1000.f;
            tile.mOriginY = -2000.f;
            tile.mSide = 300.f;
            return tile;
        }

        Vec3 position(const TileMesh& mesh, std::size_t triangle, std::size_t vertex)
        {
            const std::size_t at = triangle * 9 + vertex * 3;
            return { mesh.mPositions[at], mesh.mPositions[at + 1], mesh.mPositions[at + 2] };
        }

        Vec3 normal(const TileMesh& mesh, std::size_t triangle, std::size_t vertex)
        {
            const std::size_t at = triangle * 9 + vertex * 3;
            return { mesh.mNormals[at], mesh.mNormals[at + 1], mesh.mNormals[at + 2] };
        }

        Vec3 expectedVertex(const Lod::TerrainTile& tile, std::uint32_t i, std::uint32_t j)
        {
            const float step = tile.mSide / static_cast<float>(tile.mGridSize - 1);
            const float h = static_cast<float>(tile.mHeights[static_cast<std::size_t>(j) * tile.mGridSize + i]);
            return { tile.mOriginX + static_cast<float>(i) * step, tile.mOriginY + static_cast<float>(j) * step,
                tile.mMinHeight + h / 65535.f * (tile.mMaxHeight - tile.mMinHeight) };
        }

        void expectNearVec(const Vec3& expected, const Vec3& actual, float tolerance)
        {
            EXPECT_NEAR(expected[0], actual[0], tolerance);
            EXPECT_NEAR(expected[1], actual[1], tolerance);
            EXPECT_NEAR(expected[2], actual[2], tolerance);
        }

        Rgba pixelAt(const RgbaImage& image, std::uint32_t x, std::uint32_t y)
        {
            const std::size_t at = (static_cast<std::size_t>(y) * image.mWidth + x) * 4;
            return Rgba{ image.mPixels[at], image.mPixels[at + 1], image.mPixels[at + 2], image.mPixels[at + 3] };
        }

        void expectNearRgba(const Rgba& expected, const Rgba& actual, int tolerance)
        {
            EXPECT_LE(std::abs(static_cast<int>(expected.mR) - static_cast<int>(actual.mR)), tolerance);
            EXPECT_LE(std::abs(static_cast<int>(expected.mG) - static_cast<int>(actual.mG)), tolerance);
            EXPECT_LE(std::abs(static_cast<int>(expected.mB) - static_cast<int>(actual.mB)), tolerance);
            EXPECT_EQ(expected.mA, actual.mA);
        }

        TEST(CSMLodTileMeshTest, hasTwoTrianglesForEachGridSquare)
        {
            for (const std::uint32_t n : { 2u, 3u, 5u, 9u })
            {
                const Lod::TerrainTile tile = meshTile(n, false);
                TileMesh mesh;
                const Lod::LodError error = buildTileMesh(tile, mesh);
                ASSERT_TRUE(error.ok()) << error.describe();
                EXPECT_EQ(mesh.mGridSize, n);
                EXPECT_EQ(mesh.triangleCount(), static_cast<std::size_t>(2) * (n - 1) * (n - 1)) << "N " << n;
                EXPECT_EQ(mesh.mPositions.size(), mesh.triangleCount() * 9);
                EXPECT_EQ(mesh.mNormals.size(), mesh.triangleCount() * 9);
            }
        }

        TEST(CSMLodTileMeshTest, placesEveryTriangleVertexAtItsGridPositionInTheDocumentedOrder)
        {
            const std::uint32_t n = 4;
            const Lod::TerrainTile tile = meshTile(n, false);
            TileMesh mesh;
            ASSERT_TRUE(buildTileMesh(tile, mesh).ok());
            ASSERT_EQ(mesh.triangleCount(), 18u);

            std::size_t triangle = 0;
            for (std::uint32_t j = 0; j + 1 < n; ++j)
            {
                for (std::uint32_t i = 0; i + 1 < n; ++i)
                {
                    const Vec3 a = expectedVertex(tile, i, j);
                    const Vec3 b = expectedVertex(tile, i + 1, j);
                    const Vec3 c = expectedVertex(tile, i + 1, j + 1);
                    const Vec3 d = expectedVertex(tile, i, j + 1);
                    // Squares go west to east, then south to north; each gives (a, b, c) then (a, c, d).
                    expectNearVec(a, position(mesh, triangle, 0), 1e-3f);
                    expectNearVec(b, position(mesh, triangle, 1), 1e-3f);
                    expectNearVec(c, position(mesh, triangle, 2), 1e-3f);
                    expectNearVec(a, position(mesh, triangle + 1, 0), 1e-3f);
                    expectNearVec(c, position(mesh, triangle + 1, 1), 1e-3f);
                    expectNearVec(d, position(mesh, triangle + 1, 2), 1e-3f);
                    triangle += 2;
                }
            }
            EXPECT_EQ(triangle, mesh.triangleCount());
        }

        TEST(CSMLodTileMeshTest, putsTheFourTileCornersAtTheTilesOwnBounds)
        {
            const std::uint32_t n = 5;
            Lod::TerrainTile tile = meshTile(n, false);
            const std::size_t count = static_cast<std::size_t>(n) * n;
            tile.mHeights[0] = 0; // south-west
            tile.mHeights[n - 1] = 65535; // south-east
            tile.mHeights[count - n] = 65535; // north-west
            tile.mHeights[count - 1] = 0; // north-east
            TileMesh mesh;
            ASSERT_TRUE(buildTileMesh(tile, mesh).ok());

            const std::size_t squares = static_cast<std::size_t>(n - 1) * (n - 1);
            const std::size_t firstOfLastSquare = (squares - 1) * 2;
            const std::size_t firstOfSouthEastSquare = static_cast<std::size_t>(n - 2) * 2;
            const std::size_t firstOfNorthWestSquare = static_cast<std::size_t>(n - 2) * (n - 1) * 2;

            const Vec3 southWest = position(mesh, 0, 0); // a of square (0, 0)
            const Vec3 southEast = position(mesh, firstOfSouthEastSquare, 1); // b of square (N-2, 0)
            const Vec3 northEast = position(mesh, firstOfLastSquare, 2); // c of square (N-2, N-2)
            const Vec3 northWest = position(mesh, firstOfNorthWestSquare + 1, 2); // d of square (0, N-2)

            EXPECT_FLOAT_EQ(southWest[0], 1000.f);
            EXPECT_FLOAT_EQ(southWest[1], -2000.f);
            EXPECT_FLOAT_EQ(southWest[2], -50.f);
            EXPECT_FLOAT_EQ(southEast[0], 1300.f);
            EXPECT_FLOAT_EQ(southEast[1], -2000.f);
            EXPECT_FLOAT_EQ(southEast[2], 150.f);
            EXPECT_FLOAT_EQ(northEast[0], 1300.f);
            EXPECT_FLOAT_EQ(northEast[1], -1700.f);
            EXPECT_FLOAT_EQ(northEast[2], -50.f);
            EXPECT_FLOAT_EQ(northWest[0], 1000.f);
            EXPECT_FLOAT_EQ(northWest[1], -1700.f);
            EXPECT_FLOAT_EQ(northWest[2], 150.f);
        }

        TEST(CSMLodTileMeshTest, everyTriangleIsCounterClockwiseSeenFromAbove)
        {
            for (const std::uint32_t n : { 2u, 3u, 6u })
            {
                const Lod::TerrainTile tile = meshTile(n, false);
                TileMesh mesh;
                ASSERT_TRUE(buildTileMesh(tile, mesh).ok());
                for (std::size_t t = 0; t < mesh.triangleCount(); ++t)
                {
                    const Vec3 p0 = position(mesh, t, 0);
                    const Vec3 p1 = position(mesh, t, 1);
                    const Vec3 p2 = position(mesh, t, 2);
                    const double crossZ = (static_cast<double>(p1[0]) - p0[0]) * (static_cast<double>(p2[1]) - p0[1])
                        - (static_cast<double>(p1[1]) - p0[1]) * (static_cast<double>(p2[0]) - p0[0]);
                    EXPECT_GT(crossZ, 0.0) << "N " << n << " triangle " << t;
                }
            }
        }

        TEST(CSMLodTileMeshTest, decodesTheStoredNormalsToUnitVectors)
        {
            const std::uint32_t n = 4;
            const Lod::TerrainTile tile = meshTile(n, true);
            TileMesh mesh;
            ASSERT_TRUE(buildTileMesh(tile, mesh).ok());

            std::size_t triangle = 0;
            for (std::uint32_t j = 0; j + 1 < n; ++j)
            {
                for (std::uint32_t i = 0; i + 1 < n; ++i)
                {
                    const std::uint32_t corners[6][2]
                        = { { i, j }, { i + 1, j }, { i + 1, j + 1 }, { i, j }, { i + 1, j + 1 }, { i, j + 1 } };
                    for (std::size_t k = 0; k < 6; ++k)
                    {
                        const std::size_t vertexIndex = static_cast<std::size_t>(corners[k][1]) * n + corners[k][0];
                        Vec3 expected{};
                        Lod::decodeNormal(tile.mNormals[2 * vertexIndex], tile.mNormals[2 * vertexIndex + 1],
                            expected[0], expected[1], expected[2]);
                        const Vec3 actual = normal(mesh, triangle + k / 3, k % 3);
                        expectNearVec(expected, actual, 1e-5f);
                    }
                    triangle += 2;
                }
            }
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t)
            {
                for (std::size_t v = 0; v < 3; ++v)
                {
                    const Vec3 vec = normal(mesh, t, v);
                    const double length = std::sqrt(static_cast<double>(vec[0]) * vec[0]
                        + static_cast<double>(vec[1]) * vec[1] + static_cast<double>(vec[2]) * vec[2]);
                    EXPECT_NEAR(length, 1.0, 1e-4);
                }
            }
        }

        TEST(CSMLodTileMeshTest, usesStraightUpNormalsWhenTheTileHasNone)
        {
            const Lod::TerrainTile tile = meshTile(4, false);
            TileMesh mesh;
            ASSERT_TRUE(buildTileMesh(tile, mesh).ok());
            ASSERT_EQ(mesh.mNormals.size(), mesh.mPositions.size());
            for (std::size_t at = 0; at < mesh.mNormals.size(); at += 3)
            {
                EXPECT_FLOAT_EQ(mesh.mNormals[at], 0.f);
                EXPECT_FLOAT_EQ(mesh.mNormals[at + 1], 0.f);
                EXPECT_FLOAT_EQ(mesh.mNormals[at + 2], 1.f);
            }
        }

        TEST(CSMLodTileMeshTest, failsWithAnEmptyMeshWhenTheGridIsTooSmall)
        {
            for (const std::uint32_t n : { 0u, 1u })
            {
                Lod::TerrainTile tile = meshTile(3, false);
                tile.mGridSize = n;
                TileMesh mesh;
                const Lod::LodError error = buildTileMesh(tile, mesh);
                EXPECT_FALSE(error.ok()) << "N " << n;
                EXPECT_TRUE(mesh.mPositions.empty());
                EXPECT_TRUE(mesh.mNormals.empty());
                EXPECT_EQ(mesh.triangleCount(), 0u);
            }
        }

        TEST(CSMLodTileMeshTest, failsWithAnEmptyMeshWhenTheHeightArrayHasTheWrongSize)
        {
            Lod::TerrainTile tile = meshTile(4, false);
            tile.mHeights.pop_back();
            TileMesh mesh;
            EXPECT_FALSE(buildTileMesh(tile, mesh).ok());
            EXPECT_TRUE(mesh.mPositions.empty());
            EXPECT_EQ(mesh.triangleCount(), 0u);

            Lod::TerrainTile tooMany = meshTile(4, false);
            tooMany.mHeights.push_back(0);
            TileMesh other;
            EXPECT_FALSE(buildTileMesh(tooMany, other).ok());
            EXPECT_EQ(other.triangleCount(), 0u);
        }

        TEST(CSMLodTileMeshTest, reliefRampHasTheDocumentedEndsAndClamps)
        {
            EXPECT_EQ(reliefRamp(0.f), reliefLow);
            EXPECT_EQ(reliefRamp(1.f), reliefHigh);
            EXPECT_EQ(reliefRamp(-3.f), reliefLow);
            EXPECT_EQ(reliefRamp(7.f), reliefHigh);
            for (const float t : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
                EXPECT_EQ(reliefRamp(t).mA, 255) << "t " << t;
            EXPECT_NE(reliefLow, reliefHigh);
        }

        TEST(CSMLodTileMeshTest, heightImageIsNorthFirstWithTheRampEndsAtTheExtremes)
        {
            const std::uint32_t n = 3;
            Lod::TerrainTile tile = meshTile(n, false);
            // Row-major from the south edge: index j * 3 + i.
            tile.mHeights = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 65535 };
            const RgbaImage image = heightImage(tile);
            ASSERT_FALSE(image.empty());
            EXPECT_EQ(image.mWidth, n);
            EXPECT_EQ(image.mHeight, n);
            ASSERT_EQ(image.mPixels.size(), static_cast<std::size_t>(n) * n * 4);

            // South-west vertex (j = 0, i = 0) is the bottom-left pixel; north-east is the top-right pixel.
            EXPECT_EQ(pixelAt(image, 0, n - 1), reliefLow);
            EXPECT_EQ(pixelAt(image, n - 1, 0), reliefHigh);

            for (std::uint32_t j = 0; j < n; ++j)
            {
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    const float t = static_cast<float>(tile.mHeights[static_cast<std::size_t>(j) * n + i]) / 65535.f;
                    expectNearRgba(reliefRamp(t), pixelAt(image, i, n - 1 - j), 2);
                }
            }
        }

        TEST(CSMLodTileMeshTest, heightImageOfAFlatTileUsesTheLowEndOfTheRamp)
        {
            Lod::TerrainTile tile = meshTile(4, false);
            tile.mMinHeight = 5.f;
            tile.mMaxHeight = 5.f;
            const RgbaImage image = heightImage(tile);
            ASSERT_FALSE(image.empty());
            for (std::uint32_t y = 0; y < image.mHeight; ++y)
                for (std::uint32_t x = 0; x < image.mWidth; ++x)
                    EXPECT_EQ(pixelAt(image, x, y), reliefLow);
        }

        TEST(CSMLodTileMeshTest, picturesAreEmptyWhenTheHeightArrayHasTheWrongSize)
        {
            Lod::TerrainTile tile = meshTile(4, true);
            tile.mHeights.pop_back();
            EXPECT_TRUE(heightImage(tile).empty());
            EXPECT_TRUE(normalImage(tile).empty());
        }

        TEST(CSMLodTileMeshTest, normalImageOfAStraightUpTileIs128128255)
        {
            const Lod::TerrainTile tile = meshTile(4, false);
            const RgbaImage image = normalImage(tile);
            ASSERT_FALSE(image.empty());
            EXPECT_EQ(image.mWidth, 4u);
            EXPECT_EQ(image.mHeight, 4u);
            for (std::uint32_t y = 0; y < image.mHeight; ++y)
                for (std::uint32_t x = 0; x < image.mWidth; ++x)
                    EXPECT_EQ(pixelAt(image, x, y), (Rgba{ 128, 128, 255, 255 }));
        }

        TEST(CSMLodTileMeshTest, normalImageEncodesTheDecodedNormalsNorthFirst)
        {
            const std::uint32_t n = 3;
            Lod::TerrainTile tile = meshTile(n, true);
            const RgbaImage image = normalImage(tile);
            ASSERT_FALSE(image.empty());
            EXPECT_EQ(image.mWidth, n);
            EXPECT_EQ(image.mHeight, n);
            for (std::uint32_t j = 0; j < n; ++j)
            {
                for (std::uint32_t i = 0; i < n; ++i)
                {
                    const std::size_t index = static_cast<std::size_t>(j) * n + i;
                    float x = 0;
                    float y = 0;
                    float z = 0;
                    Lod::decodeNormal(tile.mNormals[2 * index], tile.mNormals[2 * index + 1], x, y, z);
                    const Rgba expected{ static_cast<std::uint8_t>(std::lround((0.5 + 0.5 * x) * 255)),
                        static_cast<std::uint8_t>(std::lround((0.5 + 0.5 * y) * 255)),
                        static_cast<std::uint8_t>(std::lround((0.5 + 0.5 * z) * 255)), 255 };
                    expectNearRgba(expected, pixelAt(image, i, n - 1 - j), 1);
                }
            }
        }

        TEST(CSMLodTileMeshTest, albedoImageIsEmptyWhenTheTileHasNoAlbedo)
        {
            const Lod::TerrainTile tile = meshTile(3, false);
            EXPECT_TRUE(albedoImage(tile).empty());
        }

        TEST(CSMLodTileMeshTest, albedoImageIsEmptyForABlockCompressedFormat)
        {
            Lod::TerrainTile tile = meshTile(3, false);
            tile.mHasAlbedo = true;
            tile.mAlbedoFormat = Lod::AlbedoFormat::Bc1;
            tile.mAlbedoWidth = 4;
            tile.mAlbedoHeight = 4;
            tile.mAlbedoMipCount = 1;
            tile.mAlbedo.assign(static_cast<std::size_t>(Lod::albedoByteSize(4, 4, Lod::AlbedoFormat::Bc1, 1)), 0x55);
            EXPECT_TRUE(albedoImage(tile).empty());
        }

        TEST(CSMLodTileMeshTest, albedoImageReturnsTheFirstMipLevelAsStored)
        {
            Lod::TerrainTile tile = CSMLodTest::makeTile(CSMLodTest::tileAt(0, 0, 0), 3, 0.f, 10.f, 1.f, false, true);
            const RgbaImage image = albedoImage(tile);
            ASSERT_FALSE(image.empty());
            EXPECT_EQ(image.mWidth, 2u);
            EXPECT_EQ(image.mHeight, 2u);
            ASSERT_EQ(image.mPixels.size(), 16u); // 2 x 2 x 4, not the 20 bytes of both mips
            for (std::size_t k = 0; k < image.mPixels.size(); ++k)
                EXPECT_EQ(image.mPixels[k], tile.mAlbedo[k]) << "byte " << k;
        }
    }
}
