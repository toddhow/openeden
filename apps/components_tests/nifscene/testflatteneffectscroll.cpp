// Task 0359: what flatten does with the UV controllers of an effect shader (the scrolling flame cards of Skyrim fires).
// Scenes are built by hand; no game asset is read.

#include <gtest/gtest.h>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    NifScene::UvAnimation scrollV()
    {
        NifScene::UvAnimation a;
        a.mOffset[1].mKeys = { { 0.f, 0.f, 0.f, 0.f }, { 4.f, 1.f, 0.f, 0.f } };
        a.mOffset[1].mStop = 4.f;
        return a;
    }

    struct Builder
    {
        Scene scene;

        Builder()
        {
            NifScene::Node n;
            n.mName = "root";
            scene.mNodes.push_back(n);
            scene.mRoots.push_back(0);
            NifScene::AlphaProperty alpha;
            alpha.mBlend = true;
            alpha.mSourceBlend = 6;
            alpha.mDestinationBlend = 0;
            scene.mAlphaProperties.push_back(alpha);
        }

        std::uint32_t material(NifScene::ShaderKind kind, const NifScene::UvAnimation& animation)
        {
            NifScene::ShaderMaterial m;
            m.mKind = kind;
            m.mSourceTexture = "textures/effects/flame.dds";
            m.mBaseColor = { 1.f, 1.f, 1.f, 1.f };
            m.mBaseColorScale = 1.f;
            m.mFlags2 = ShaderFlags::sDoubleSided;
            m.mUVOffset = { 0.5f, 0.25f };
            m.mUVScale = { 2.f, 4.f };
            m.mUvAnimation = animation;
            scene.mMaterials.push_back(m);
            return static_cast<std::uint32_t>(scene.mMaterials.size() - 1);
        }

        void card(std::uint32_t materialIndex)
        {
            NifScene::Node n;
            n.mName = "card";
            n.mParent = 0;
            scene.mNodes.push_back(n);
            NifScene::Geometry g;
            g.mNode = static_cast<std::uint32_t>(scene.mNodes.size() - 1);
            g.mPositions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
            g.mNormals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
            g.mUV0 = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            g.mTriangles = { 0, 1, 2, 0, 2, 3 };
            g.mMaterial = materialIndex;
            g.mAlpha = 0;
            scene.mGeometries.push_back(g);
            scene.mNodes.back().mGeometry = static_cast<std::uint32_t>(scene.mGeometries.size() - 1);
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mEffects = true;
        return limits;
    }
}

TEST(NifSceneFlattenEffectScroll, an_effect_shape_with_no_uv_controller_is_baked_as_before)
{
    Builder b;
    b.card(b.material(NifScene::ShaderKind::Effect, {}));
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mScroll, nullptr);
    EXPECT_EQ(model.mShapes[0].mKey.mScroll, 0u);
    ASSERT_EQ(model.mShapes[0].mUV.size(), 4u);
    EXPECT_FLOAT_EQ(model.mShapes[0].mUV[2][0], 1.f * 2.f + 0.5f); // uv * scale + offset
    EXPECT_FLOAT_EQ(model.mShapes[0].mUV[2][1], 1.f * 4.f + 0.25f);
}

TEST(NifSceneFlattenEffectScroll, a_scrolled_shape_keeps_its_uv_raw_and_carries_the_animation)
{
    Builder b;
    NifScene::UvAnimation a = scrollV();
    b.card(b.material(NifScene::ShaderKind::Effect, a));
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    const Shape& shape = model.mShapes[0];
    ASSERT_NE(shape.mScroll, nullptr);
    EXPECT_NE(shape.mKey.mScroll, 0u);
    EXPECT_EQ(shape.mScroll->mOffset[1].mKeys.size(), 2u);
    // the texture matrix applies the shader's offset and scale, so the coordinates stay as the file stores them
    EXPECT_FLOAT_EQ(shape.mUV[2][0], 1.f);
    EXPECT_FLOAT_EQ(shape.mUV[2][1], 1.f);
    // the property's own values are the base of the components no controller moves
    EXPECT_FLOAT_EQ(shape.mScroll->mBaseOffset[0], 0.5f);
    EXPECT_FLOAT_EQ(shape.mScroll->mBaseScale[1], 4.f);
}

TEST(NifSceneFlattenEffectScroll, shapes_of_one_scrolling_material_merge_and_other_materials_do_not)
{
    Builder b;
    const std::uint32_t one = b.material(NifScene::ShaderKind::Effect, scrollV());
    NifScene::UvAnimation other = scrollV();
    other.mOffset[1].mKeys[1].mValue = 2.f;
    const std::uint32_t two = b.material(NifScene::ShaderKind::Effect, other);
    b.card(one);
    b.card(one);
    b.card(two);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 2u);
    EXPECT_NE(model.mShapes[0].mKey.mScroll, model.mShapes[1].mKey.mScroll);
    const Shape& merged = model.mShapes[0].mPositions.size() == 8 ? model.mShapes[0] : model.mShapes[1];
    EXPECT_EQ(merged.mPositions.size(), 8u);
}

TEST(NifSceneFlattenEffectScroll, a_lighting_shader_is_never_scrolled_here)
{
    Builder b;
    const std::uint32_t m = b.material(NifScene::ShaderKind::Lighting, scrollV());
    b.scene.mTextureSets.push_back({});
    b.scene.mTextureSets[0].mTextures = { "textures\a.dds" };
    b.scene.mMaterials[m].mTextureSet = 0;
    b.card(m);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mShapes.size(), 1u);
    EXPECT_EQ(model.mShapes[0].mScroll, nullptr);
    EXPECT_FLOAT_EQ(model.mShapes[0].mUV[2][0], 1.f * 2.f + 0.5f);
}
