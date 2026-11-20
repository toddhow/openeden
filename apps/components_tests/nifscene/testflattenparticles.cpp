// Task 0359 stage b: what flatten does with the particle systems of a Skyrim model. Scenes are built by hand.

#include <gtest/gtest.h>

#include <limits>

#include <components/nifscene/flatten.hpp>

namespace
{
    using namespace NifScene::Flat;
    using NifScene::Scene;

    struct Builder
    {
        Scene scene;

        Builder()
        {
            NifScene::Node root;
            root.mName = "root";
            root.mTransform.mTranslation = { 10.f, 0.f, 0.f };
            scene.mNodes.push_back(root);
            scene.mRoots.push_back(0);
            NifScene::AlphaProperty alpha;
            alpha.mBlend = true;
            alpha.mSourceBlend = 6;
            alpha.mDestinationBlend = 0;
            scene.mAlphaProperties.push_back(alpha);
            NifScene::ShaderMaterial m;
            m.mKind = NifScene::ShaderKind::Effect;
            m.mSourceTexture = "textures\\effects\\fireatlas.dds";
            m.mGreyscaleTexture = "textures\\effects\\gradients\\fire.dds";
            m.mFlags1 = ShaderFlags::sGreyscaleToPaletteColor;
            m.mBaseColor = { 1.f, 0.5f, 0.25f, 0.8f };
            m.mBaseColorScale = 2.f;
            scene.mMaterials.push_back(m);
        }

        std::uint32_t node(const char* name, std::uint32_t parent, NifScene::Transform t = {}, std::uint32_t flags = 0)
        {
            NifScene::Node n;
            n.mName = name;
            n.mParent = parent;
            n.mTransform = t;
            n.mFlags = flags;
            scene.mNodes.push_back(n);
            return static_cast<std::uint32_t>(scene.mNodes.size() - 1);
        }

        NifScene::ParticleSystem& system(std::uint32_t systemNode, std::uint32_t emitterNode)
        {
            NifScene::ParticleSystem s;
            s.mName = "flames";
            s.mNode = systemNode;
            s.mEmitterNode = emitterNode;
            s.mMaterial = 0;
            s.mAlpha = 0;
            s.mBirthRate = 12.f;
            s.mMaxParticles = 16;
            scene.mParticleSystems.push_back(s);
            return scene.mParticleSystems.back();
        }
    };

    Limits on()
    {
        Limits limits;
        limits.mEffects = true;
        limits.mParticles = true;
        return limits;
    }
}

TEST(NifSceneFlattenParticles, nothing_is_collected_unless_asked_for)
{
    Builder b;
    b.system(b.node("ps", 0), 0xFFFFFFFFu);
    Limits only;
    only.mEffects = true;
    EXPECT_TRUE(flatten(b.scene, only).mParticles.empty());
    Limits particlesOnly;
    particlesOnly.mParticles = true; // no effect shapes: no look to draw them with
    EXPECT_TRUE(flatten(b.scene, particlesOnly).mParticles.empty());
    EXPECT_EQ(flatten(b.scene, on()).mParticles.size(), 1u);
}

TEST(NifSceneFlattenParticles, the_emitter_object_places_the_flame_in_the_model_frame)
{
    Builder b;
    NifScene::Transform t;
    t.mTranslation = { 0.f, 5.f, 7.f };
    t.mScale = 2.f;
    const std::uint32_t emitter = b.node("emitter", 0, t);
    b.system(b.node("ps", 0), emitter);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mParticles.size(), 1u);
    const ParticleSystem& p = model.mParticles[0];
    EXPECT_FLOAT_EQ(p.mCentre[0], 10.f); // the root's own offset
    EXPECT_FLOAT_EQ(p.mCentre[1], 5.f);
    EXPECT_FLOAT_EQ(p.mCentre[2], 7.f);
    EXPECT_FLOAT_EQ(p.mEmitterScale, 2.f);
    EXPECT_FLOAT_EQ(p.mSystemScale, 1.f);
}

TEST(NifSceneFlattenParticles, a_system_without_an_emitter_object_uses_its_own_node)
{
    Builder b;
    NifScene::Transform t;
    t.mTranslation = { 1.f, 2.f, 3.f };
    t.mScale = 1.2f;
    b.system(b.node("ps", 0, t), 0xFFFFFFFFu);
    const ParticleSystem p = flatten(b.scene, on()).mParticles.at(0);
    EXPECT_FLOAT_EQ(p.mCentre[0], 11.f);
    EXPECT_FLOAT_EQ(p.mSystemScale, 1.2f);
}

TEST(NifSceneFlattenParticles, the_look_is_an_effect_shapes_key)
{
    Builder b;
    b.system(b.node("ps", 0), 0xFFFFFFFFu);
    const FlatModel model = flatten(b.scene, on());
    ASSERT_EQ(model.mParticles.size(), 1u);
    const MaterialKey& key = model.mParticles[0].mKey;
    EXPECT_TRUE(key.mEffect);
    EXPECT_EQ(key.mDiffuse, "textures\\effects\\fireatlas.dds");
    EXPECT_EQ(key.mPalette, "textures\\effects\\gradients\\fire.dds");
    EXPECT_TRUE(key.mBlend);
    EXPECT_EQ(key.mSourceBlend, 6);
    EXPECT_EQ(key.mDestinationBlend, 0);
    EXPECT_FLOAT_EQ(key.mEffectColor[0], 2.f); // base colour times its scale
    EXPECT_FLOAT_EQ(key.mEffectColor[1], 1.f);
    EXPECT_FLOAT_EQ(key.mEffectColor[3], 0.8f);
}

TEST(NifSceneFlattenParticles, hidden_systems_and_systems_that_emit_nothing_are_dropped)
{
    Builder b;
    b.system(b.node("hidden", 0, {}, 1u), 0xFFFFFFFFu);
    b.system(b.node("fine", 0), 0xFFFFFFFFu);
    b.system(b.node("silent", 0), 0xFFFFFFFFu).mBirthRate = 0.f;
    b.system(b.node("nolook", 0), 0xFFFFFFFFu).mMaterial = 0xFFFFFFFFu;
    const FlatModel model = flatten(b.scene, on());
    EXPECT_EQ(model.mParticles.size(), 1u);
}

TEST(NifSceneFlattenParticles, a_system_that_refracts_is_left_out)
{
    Builder b;
    b.scene.mMaterials[0].mFlags1 |= ShaderFlags::sRefraction;
    b.system(b.node("ps", 0), 0xFFFFFFFFu);
    EXPECT_TRUE(flatten(b.scene, on()).mParticles.empty());
}

TEST(NifSceneFlattenParticles, gravity_in_the_systems_own_frame_is_turned_with_it_and_scaled)
{
    Builder b;
    NifScene::Transform t; // a quarter turn about X: local Z goes to model -Y
    t.mRotation = { 1.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 1.f, 0.f };
    t.mScale = 2.f;
    NifScene::ParticleSystem& s = b.system(b.node("ps", 0, t), 0xFFFFFFFFu);
    s.mGravity = { 0.f, 0.f, 10.f };
    s.mGravityWorldAligned = false;
    ParticleSystem local = flatten(b.scene, on()).mParticles.at(0);
    EXPECT_NEAR(local.mGravity[1], -10.f * 2.f, 1e-4f);
    EXPECT_NEAR(local.mGravity[2], 0.f, 1e-4f);
    b.scene.mParticleSystems[0].mGravityWorldAligned = true; // stays along model Z, scaled
    ParticleSystem world = flatten(b.scene, on()).mParticles.at(0);
    EXPECT_NEAR(world.mGravity[2], 10.f * 2.f, 1e-4f);
}

TEST(NifSceneFlattenParticles, a_node_with_a_broken_transform_does_not_take_the_model_with_it)
{
    Builder b;
    NifScene::Transform t;
    t.mTranslation = { std::numeric_limits<float>::infinity(), 0.f, 0.f };
    b.system(b.node("ps", 0, t), 0xFFFFFFFFu);
    EXPECT_NO_THROW(flatten(b.scene, on()));
}
