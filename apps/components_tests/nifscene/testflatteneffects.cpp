// Task 0191 (visible spells): what flatten keeps of a BSEffectShaderProperty shape under Limits::mEffects (the glow
// cards and cones of the spell projectiles). Scenes are built by hand; no game asset is read.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    constexpr float notANumber = std::numeric_limits<float>::quiet_NaN();

    struct EffectSpec
    {
        std::string source = "textures\\effects\\glow.dds";
        std::string greyscale;
        std::array<float, 4> baseColor{ 1.f, 0.5f, 0.25f, 0.8f };
        float scale = 2.f;
        std::uint32_t flags1 = 0;
        std::uint32_t flags2 = ShaderFlags::sDoubleSided | ShaderFlags::sVertexColors;
        bool additive = true;
        bool colors = false;
        bool uvs = true;
    };

    struct Builder
    {
        Scene scene;

        Builder()
        {
            NifScene::Node n;
            n.mName = "root";
            scene.mNodes.push_back(n);
            scene.mRoots.push_back(0);
        }

        std::uint32_t effect(const EffectSpec& spec)
        {
            NifScene::ShaderMaterial m;
            m.mKind = NifScene::ShaderKind::Effect;
            m.mSourceTexture = spec.source;
            m.mGreyscaleTexture = spec.greyscale;
            m.mBaseColor = spec.baseColor;
            m.mBaseColorScale = spec.scale;
            m.mFlags1 = spec.flags1;
            m.mFlags2 = spec.flags2;
            scene.mMaterials.push_back(m);
            NifScene::AlphaProperty alpha;
            alpha.mBlend = true;
            alpha.mSourceBlend = 6; // source alpha
            alpha.mDestinationBlend = spec.additive ? 0 : 7; // one, or inverse source alpha
            scene.mAlphaProperties.push_back(alpha);

            NifScene::Node n;
            n.mName = "card";
            n.mParent = 0;
            scene.mNodes.push_back(n);
            NifScene::Geometry g;
            g.mName = "card";
            g.mNode = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            if (spec.uvs)
                g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            if (spec.colors)
                g.mColors = { { 1, 1, 1, 0.5f }, { 1, 1, 1, 0.5f }, { 1, 1, 1, 0.5f }, { 1, 1, 1, 0.5f } };
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
            g.mAlpha = static_cast<std::uint32_t>(scene.mAlphaProperties.size() - 1);
            scene.mGeometries.push_back(g);
            scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
            return static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mEffects = true;
        return limits;
    }

    bool near(float a, float b, float eps = 1e-5f)
    {
        return std::fabs(a - b) <= eps;
    }
}

TEST(NifSceneFlattenEffects, with_the_option_off_an_effect_shape_is_left_out_as_before)
{
    Builder b;
    b.effect({});
    const FlatModel model = flatten(b.scene);
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedEffect, 1u);
}

TEST(NifSceneFlattenEffects, with_the_option_on_an_effect_shape_is_drawn)
{
    Builder b;
    b.effect({});
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mReport.mSkippedEffect, 0u);
    EXPECT_EQ(model.mReport.mDrawn, 1u);
    EXPECT_EQ(model.mShapes[0].mIndices.size(), 6u);
}

TEST(NifSceneFlattenEffects, the_key_carries_the_source_texture_and_the_scaled_base_colour)
{
    Builder b;
    b.effect({});
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const MaterialKey& key = model.mShapes[0].mKey;
    EXPECT_TRUE(key.mEffect);
    EXPECT_EQ(key.mDiffuse, "textures\\effects\\glow.dds");
    EXPECT_TRUE(near(key.mEffectColor[0], 2.0f)); // 1.0 * 2
    EXPECT_TRUE(near(key.mEffectColor[1], 1.0f)); // 0.5 * 2
    EXPECT_TRUE(near(key.mEffectColor[2], 0.5f)); // 0.25 * 2
    EXPECT_TRUE(near(key.mEffectColor[3], 0.8f)); // alpha is not scaled
    EXPECT_TRUE(near(key.mAlpha, 0.8f));
}

TEST(NifSceneFlattenEffects, the_blend_is_the_alpha_property_and_the_shape_is_double_sided_when_flagged)
{
    Builder b;
    b.effect({});
    const MaterialKey& key = flatten(b.scene, on()).mShapes.at(0).mKey;
    EXPECT_TRUE(key.mBlend);
    EXPECT_EQ(key.mSourceBlend, 6);
    EXPECT_EQ(key.mDestinationBlend, 0); // additive: source alpha, one
    EXPECT_TRUE(key.mDoubleSided);

    Builder c;
    EffectSpec single;
    single.flags2 = 0;
    c.effect(single);
    EXPECT_FALSE(flatten(c.scene, on()).mShapes.at(0).mKey.mDoubleSided);
}

TEST(NifSceneFlattenEffects, a_base_colour_scale_of_zero_means_one)
{
    Builder b;
    EffectSpec spec;
    spec.scale = 0.f;
    b.effect(spec);
    const MaterialKey& key = flatten(b.scene, on()).mShapes.at(0).mKey;
    EXPECT_TRUE(near(key.mEffectColor[0], 1.0f));
    EXPECT_TRUE(near(key.mEffectColor[1], 0.5f));
}

TEST(NifSceneFlattenEffects, a_base_colour_that_is_not_a_number_is_white_at_full_alpha)
{
    Builder b;
    EffectSpec spec;
    spec.baseColor = { notANumber, 1.f, 1.f, notANumber };
    b.effect(spec);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const MaterialKey& key = model.mShapes[0].mKey;
    for (float c : key.mEffectColor)
        EXPECT_TRUE(std::isfinite(c));
    EXPECT_TRUE(near(key.mEffectColor[0], 2.0f)); // the bad channel is 1, times the scale 2
    EXPECT_TRUE(near(key.mEffectColor[3], 1.0f));
}

TEST(NifSceneFlattenEffects, a_refracting_effect_shape_stays_out)
{
    Builder b;
    EffectSpec spec;
    spec.flags1 = ShaderFlags::sRefraction;
    b.effect(spec);
    const FlatModel model = flatten(b.scene, on());
    EXPECT_TRUE(model.mShapes.empty());
    EXPECT_EQ(model.mReport.mSkippedEffect, 1u);
}

TEST(NifSceneFlattenEffects, an_effect_shape_with_no_source_texture_is_drawn_untextured)
{
    Builder b;
    EffectSpec spec;
    spec.source.clear();
    b.effect(spec);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_TRUE(model.mShapes[0].mKey.mDiffuse.empty());
}

TEST(NifSceneFlattenEffects, vertex_colours_are_kept_with_their_alpha_when_the_shader_says_so)
{
    Builder b;
    EffectSpec spec;
    spec.colors = true;
    spec.flags1 = ShaderFlags::sVertexAlpha;
    b.effect(spec);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& shape = model.mShapes[0];
    ASSERT_EQ(shape.mColors.size(), 4u);
    EXPECT_TRUE(near(shape.mColors[0][3], 0.5f));
    EXPECT_TRUE(shape.mKey.mVertexColors);
}

TEST(NifSceneFlattenEffects, shapes_of_equal_effect_keys_merge_and_unequal_colours_do_not)
{
    Builder b;
    b.effect({});
    b.effect({});
    EffectSpec other;
    other.baseColor = { 0.f, 0.f, 1.f, 1.f };
    b.effect(other);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_EQ(model.mShapes[0].mMergedFrom, 2u);
    EXPECT_EQ(model.mShapes[1].mMergedFrom, 1u);
}

TEST(NifSceneFlattenEffects, a_lighting_shape_is_not_marked_as_an_effect)
{
    Builder b;
    b.effect({});
    NifScene::ShaderMaterial m;
    m.mKind = NifScene::ShaderKind::Lighting;
    NifScene::TextureSet set;
    set.mTextures = { "textures\\a_d.dds" };
    b.scene.mTextureSets.push_back(set);
    m.mTextureSet = 0;
    b.scene.mMaterials.push_back(m);
    NifScene::Node n;
    n.mName = "lit";
    n.mParent = 0;
    b.scene.mNodes.push_back(n);
    NifScene::Geometry g = b.scene.mGeometries[0];
    g.mNode = static_cast<std::uint32_t>(b.scene.mNodes.size() - 1);
    g.mMaterial = static_cast<std::uint32_t>(b.scene.mMaterials.size() - 1);
    g.mAlpha = NifScene::sNone;
    b.scene.mGeometries.push_back(g);
    b.scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(b.scene.mGeometries.size() - 1);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 2u);
    int effects = 0;
    for (const Shape& shape : model.mShapes)
        effects += shape.mKey.mEffect ? 1 : 0;
    EXPECT_EQ(effects, 1);
}

TEST(NifSceneFlattenEffects, the_palette_texture_is_named_only_when_the_flags_say_the_colour_comes_from_it)
{
    Builder b;
    EffectSpec withFlag;
    withFlag.greyscale = "textures\\effects\\gradients\\fire.dds";
    withFlag.flags1 = ShaderFlags::sGreyscaleToPaletteColor;
    b.effect(withFlag);
    EXPECT_EQ(flatten(b.scene, on()).mShapes.at(0).mKey.mPalette, "textures\\effects\\gradients\\fire.dds");

    Builder c;
    EffectSpec without;
    without.greyscale = "textures\\effects\\gradients\\fire.dds";
    c.effect(without);
    EXPECT_TRUE(flatten(c.scene, on()).mShapes.at(0).mKey.mPalette.empty());
}

TEST(NifSceneFlattenEffects, a_base_alpha_of_zero_is_drawn_as_one_because_a_controller_animates_it)
{
    Builder b;
    EffectSpec spec;
    spec.baseColor = { 1.f, 1.f, 1.f, 0.f };
    b.effect(spec);
    const MaterialKey& key = flatten(b.scene, on()).mShapes.at(0).mKey;
    EXPECT_TRUE(near(key.mEffectColor[3], 1.0f));
    EXPECT_TRUE(near(key.mAlpha, 1.0f));
}
