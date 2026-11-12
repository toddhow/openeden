// Task 0125: the pure half of colouring a Skyrim SE face (components/nifscene/facetint.*): the detail map times the
// actor's baked tint, and the texture path forms the face's own slots use. Hand-made pixels; no game asset.

#include <gtest/gtest.h>

#include <array>

#include <components/nifscene/facetint.hpp>

namespace
{
    using NifScene::FaceTint::Image;

    Image solid(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        Image image;
        image.mWidth = w;
        image.mHeight = h;
        for (std::uint32_t i = 0; i < w * h; ++i)
            image.mRgba.insert(image.mRgba.end(), { r, g, b, a });
        return image;
    }

    std::array<int, 4> pixel(const Image& image, std::uint32_t x, std::uint32_t y)
    {
        const std::size_t i = (static_cast<std::size_t>(y) * image.mWidth + x) * 4;
        return { image.mRgba[i], image.mRgba[i + 1], image.mRgba[i + 2], image.mRgba[i + 3] };
    }
}

TEST(NifSceneFaceTint, the_product_is_base_times_tint_times_two)
{
    Image out;
    std::string error;
    // 100 * 128 * 2 / 255 = 100.39, 50 * 255 * 2 / 255 = 100, 200 * 64 * 2 / 255 = 100.39
    ASSERT_TRUE(
        NifScene::FaceTint::composite(solid(2, 2, 100, 50, 200, 255), solid(2, 2, 128, 255, 64, 77), out, error))
        << error;
    ASSERT_EQ(out.mWidth, 2u);
    ASSERT_EQ(out.mHeight, 2u);
    ASSERT_EQ(out.mRgba.size(), 16u);
    EXPECT_EQ(pixel(out, 1, 1), (std::array<int, 4>{ 100, 100, 100, 255 })); // alpha is the base's, not the tint's
}

TEST(NifSceneFaceTint, a_bright_product_is_clamped_to_255)
{
    Image out;
    std::string error;
    ASSERT_TRUE(
        NifScene::FaceTint::composite(solid(1, 1, 200, 200, 200, 255), solid(1, 1, 200, 200, 200, 255), out, error));
    EXPECT_EQ(pixel(out, 0, 0), (std::array<int, 4>{ 255, 255, 255, 255 }));
}

TEST(NifSceneFaceTint, a_black_tint_gives_black_and_the_base_alpha_survives)
{
    Image out;
    std::string error;
    ASSERT_TRUE(NifScene::FaceTint::composite(solid(1, 1, 90, 90, 90, 10), solid(1, 1, 0, 0, 0, 255), out, error));
    EXPECT_EQ(pixel(out, 0, 0), (std::array<int, 4>{ 0, 0, 0, 10 }));
}

TEST(NifSceneFaceTint, a_tint_of_another_size_is_sampled_at_the_same_texture_coordinates)
{
    // A uniform 2 x 2 tint over an 8 x 4 base gives the uniform result everywhere.
    Image out;
    std::string error;
    ASSERT_TRUE(
        NifScene::FaceTint::composite(solid(8, 4, 100, 100, 100, 255), solid(2, 2, 64, 64, 64, 255), out, error));
    ASSERT_EQ(out.mWidth, 8u);
    ASSERT_EQ(out.mHeight, 4u);
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 8; ++x)
            EXPECT_EQ(pixel(out, x, y), (std::array<int, 4>{ 50, 50, 50, 255 })) << x << "," << y; // 100*64*2/255
}

TEST(NifSceneFaceTint, a_tint_gradient_is_interpolated_not_stepped)
{
    // Tint: left column 0, right column 254 (2 x 1). Base 127 everywhere (16 x 1): the result rises monotonically
    // from the left to the right and the first and last pixels are near the tint's ends.
    Image tint;
    tint.mWidth = 2;
    tint.mHeight = 1;
    tint.mRgba = { 0, 0, 0, 255, 254, 254, 254, 255 };
    Image out;
    std::string error;
    ASSERT_TRUE(NifScene::FaceTint::composite(solid(16, 1, 127, 127, 127, 255), tint, out, error));
    int previous = -1;
    int distinct = 0;
    for (std::uint32_t x = 0; x < 16; ++x)
    {
        const int v = pixel(out, x, 0)[0];
        EXPECT_GE(v, previous) << x;
        if (v != previous)
            ++distinct;
        previous = v;
    }
    EXPECT_GE(distinct, 8); // a stepped (nearest) sample of a 2 pixel tint would have 2
    EXPECT_LT(pixel(out, 0, 0)[0], 40);
    EXPECT_GT(pixel(out, 15, 0)[0], 215);
}

TEST(NifSceneFaceTint, empty_or_inconsistent_images_are_refused_with_a_reason)
{
    Image out = solid(1, 1, 1, 1, 1, 1);
    std::string error;
    EXPECT_FALSE(NifScene::FaceTint::composite(Image{}, solid(1, 1, 1, 1, 1, 1), out, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(out.mRgba.empty());
    error.clear();
    EXPECT_FALSE(NifScene::FaceTint::composite(solid(1, 1, 1, 1, 1, 1), Image{}, out, error));
    EXPECT_FALSE(error.empty());
    Image bad = solid(2, 2, 1, 1, 1, 1);
    bad.mRgba.pop_back();
    error.clear();
    EXPECT_FALSE(NifScene::FaceTint::composite(bad, solid(1, 1, 1, 1, 1, 1), out, error));
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(NifScene::FaceTint::composite(solid(1, 1, 1, 1, 1, 1), bad, out, error));
    EXPECT_FALSE(error.empty());
}

TEST(NifSceneFaceTint, a_texture_path_without_the_textures_root_gets_it)
{
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath("Actors\\Character\\Male\\MaleHead.dds"),
        "textures/actors/character/male/malehead.dds");
}

TEST(NifSceneFaceTint, a_data_prefixed_path_loses_data_and_keeps_textures)
{
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath(
                  "data\\Textures\\Actors\\Character\\FaceGenData\\FaceTint\\Skyrim.esm\\00013255.dds"),
        "textures/actors/character/facegendata/facetint/skyrim.esm/00013255.dds");
}

TEST(NifSceneFaceTint, a_path_that_already_has_the_root_is_only_lowered_and_flipped)
{
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath("textures\\Actors\\Character\\Hair\\HairLong.dds"),
        "textures/actors/character/hair/hairlong.dds");
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath("Textures/A.dds"), "textures/a.dds");
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath(""), "");
}

// Task skyext: 60 vanilla Skyrim SE models (295 references, 120 distinct paths) name their textures under the build
// machine's folder "textures\skyrimhd\build\pc\data\textures\...", which no archive holds; 119 of the 120 exist in the
// archives as plain "textures\...", the 120th is in no archive.
TEST(NifSceneFaceTint, the_skyrimhd_build_folder_is_stripped_to_the_plain_textures_path)
{
    EXPECT_EQ(
        NifScene::FaceTint::normaliseTexturePath("textures\\SkyrimHD\\build\\pc\\data\\textures\\plants\\thistle01.dds"),
        "textures/plants/thistle01.dds");
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath(
                  "Textures/skyrimhd/build/pc/data/textures/landscape/grass/swordfern_n.dds"),
        "textures/landscape/grass/swordfern_n.dds");
    // Without the textures root in front the same folder is still the marker.
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath("skyrimhd\\build\\pc\\data\\textures\\a.dds"), "textures/a.dds");
    // A path that merely mentions the word elsewhere keeps its folder.
    EXPECT_EQ(NifScene::FaceTint::normaliseTexturePath("textures\\skyrimhdfoo\\a.dds"), "textures/skyrimhdfoo/a.dds");
}
