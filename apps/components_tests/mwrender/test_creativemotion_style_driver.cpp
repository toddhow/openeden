// Tests of the style layer inside CreativeMotionPoseDriver (task 0053), written from the task spec's "Interfaces"
// section and the driver's public header only. A synthetic Morrowind-named humanoid of Anim::Transform nodes and
// synthetic animation sources; no GPU, window, network or game data.

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/creativemotiondriver.hpp>
#include <apps/openmw/mwrender/creativemotionstyle.hpp>

#include <components/animation/bones.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/vsgutil/name.hpp>

#include <opas/style/StyleProfile.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using MWRender::ActorState;
    using MWRender::CreativeMotionActiveSource;
    using MWRender::CreativeMotionPoseDriver;
    using MWRender::MotionKind;
    using opas::style::StyleProfile;
    using ActiveArray = std::array<CreativeMotionActiveSource, MWRender::sNumBlendMasks>;

    constexpr float sDt = 1.f / 60.f;
    constexpr float sTransition = 0.25f;

    std::string toLower(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    const std::map<std::string, uint32_t> sGroups
        = { { "bip01 spine1", 1 }, { "bip01 l clavicle", 2 }, { "bip01 r clavicle", 3 } };

    float quatDot(const vsg::quat& a, const vsg::quat& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    float quatAngle(const vsg::quat& a, const vsg::quat& b)
    {
        return 2.f * std::acos(std::min(1.f, std::abs(quatDot(a, b))));
    }

    // Quaternions q and -q are the same rotation: compare after aligning their signs.
    void expectQuatNear(const vsg::quat& actual, const vsg::quat& expected, float tol)
    {
        const float sign = quatDot(actual, expected) < 0.f ? -1.f : 1.f;
        EXPECT_NEAR(actual.x * sign, expected.x, tol);
        EXPECT_NEAR(actual.y * sign, expected.y, tol);
        EXPECT_NEAR(actual.z * sign, expected.z, tol);
        EXPECT_NEAR(actual.w * sign, expected.w, tol);
    }

    float distance(const vsg::vec3& a, const vsg::vec3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Bone name (as in Morrowind), parent index and rest translation in engine units. Parents come first.
    struct BoneDef
    {
        const char* name;
        int parent;
        vsg::vec3 translation;
    };
    const std::vector<BoneDef> sBoneDefs = {
        { "Bip01", -1, { 0.f, 0.f, 0.f } }, // 0
        { "Bip01 Pelvis", 0, { 0.f, 0.f, 66.f } }, // 1
        { "Bip01 Spine", 1, { 0.f, 0.f, 8.f } }, // 2
        { "Bip01 Spine1", 2, { 0.f, 0.f, 8.f } }, // 3
        { "Bip01 Neck", 3, { 0.f, 0.f, 15.f } }, // 4
        { "Bip01 Head", 4, { 0.f, 0.f, 5.f } }, // 5
        { "Bip01 L Clavicle", 3, { -5.f, 0.f, 12.f } }, // 6
        { "Bip01 L UpperArm", 6, { -14.f, 0.f, 0.f } }, // 7
        { "Bip01 L Forearm", 7, { -20.f, 0.f, 0.f } }, // 8
        { "Bip01 R Clavicle", 3, { 5.f, 0.f, 12.f } }, // 9
        { "Bip01 R UpperArm", 9, { 14.f, 0.f, 0.f } }, // 10
        { "Bip01 R Forearm", 10, { 20.f, 0.f, 0.f } }, // 11
        { "Bip01 L Thigh", 1, { -6.f, 0.f, -1.f } }, // 12
        { "Bip01 L Calf", 12, { 0.f, 0.f, -30.f } }, // 13
        { "Bip01 R Thigh", 1, { 6.f, 0.f, -1.f } }, // 14
        { "Bip01 R Calf", 14, { 0.f, 0.f, -30.f } }, // 15
        { "Bip01 Tail", 1, { 0.f, -10.f, 0.f } }, // 16: not a styled bone
    };

    struct Humanoid
    {
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
        std::vector<vsg::ref_ptr<Anim::Transform>> nodes;
        std::unique_ptr<Anim::Bones> bones;

        Humanoid()
        {
            for (const BoneDef& def : sBoneDefs)
            {
                auto node = Anim::Transform::create();
                vsgUtil::setName(*node, def.name);
                node->translation = def.translation;
                (def.parent < 0 ? *root : *nodes[static_cast<size_t>(def.parent)]).addChild(node);
                nodes.push_back(node);
            }
            bones = std::make_unique<Anim::Bones>(toLower, *root, sGroups);
        }
    };

    template <class T>
    vsg::ref_ptr<Anim::Linear<T>> constant(const T& value)
    {
        auto channel = Anim::make_channel<Anim::Linear<T>>();
        channel->keys = { { 0.f, value } };
        return channel;
    }

    // An animation source holding every bone still at its rest translation. The rotations are all identity for
    // amount 0, and a small different rotation per bone otherwise.
    struct Source
    {
        vsg::ref_ptr<Anim::ControllerMap> map{ new Anim::ControllerMap };

        explicit Source(float amount)
        {
            map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
            for (size_t i = 0; i < sBoneDefs.size(); ++i)
            {
                vsg::ref_ptr<Anim::TransformController> controller(new Anim::TransformController);
                controller->translate = constant(sBoneDefs[i].translation);
                const float angle = amount * (0.05f + 0.01f * static_cast<float>(i));
                controller->rotate = constant(vsg::quat(angle, vsg::vec3(0.f, 1.f, 0.f)));
                map->map[sBoneDefs[i].name] = controller;
            }
        }
        const Anim::ControllerMap* get() const { return map.get(); }
    };

    ActiveArray allGroups(const Source& source, std::string_view group, float time)
    {
        ActiveArray active{};
        for (auto& a : active)
            a = CreativeMotionActiveSource{ source.get(), group, time };
        return active;
    }

    // Everything the driver outputs for one frame, as floats, for exact comparison.
    using Snapshot = std::vector<float>;

    Snapshot snapshot(const CreativeMotionPoseDriver& driver)
    {
        Snapshot out;
        for (size_t i = 0; i < driver.getSkeleton().bones.size(); ++i)
        {
            const vsg::vec3& t = driver.getBoneTranslation(i);
            const vsg::quat& q = driver.getBoneRotation(i);
            out.insert(out.end(), { t.x, t.y, t.z, q.x, q.y, q.z, q.w });
        }
        return out;
    }

    // A driver on its own rig.
    struct Scene
    {
        Humanoid rig;
        CreativeMotionPoseDriver driver{ *rig.bones };

        Scene() { EXPECT_TRUE(driver.isValid()); }

        int bone(std::string_view normalizedName) const
        {
            const int i = driver.getBoneIndex(normalizedName);
            EXPECT_GE(i, 0) << normalizedName;
            return i;
        }
        vsg::quat rotation(std::string_view name) const
        {
            return driver.getBoneRotation(static_cast<size_t>(bone(name)));
        }
        vsg::vec3 translation(std::string_view name) const
        {
            return driver.getBoneTranslation(static_cast<size_t>(bone(name)));
        }
    };

    // The same scripted sequence for any scene: ten frames of idle, ten of walkforward on another source.
    std::vector<Snapshot> scriptedSequence(Scene& scene, const Source& idle, const Source& walk, bool varyState = false)
    {
        std::vector<Snapshot> frames;
        for (int f = 0; f < 20; ++f)
        {
            if (varyState)
            {
                ActorState state;
                state.mSpeedMps = f < 10 ? 0.f : 1.5f;
                state.mRunning = f > 15;
                state.mWeaponDrawn = f % 2 == 0;
                scene.driver.setActorState(state);
            }
            const bool walking = f >= 10;
            scene.driver.update(
                sDt, allGroups(walking ? walk : idle, walking ? "walkforward" : "idle", f * sDt), sTransition);
            frames.push_back(snapshot(scene.driver));
        }
        return frames;
    }

    // A profile with only a forward slouch: nothing else may change the pose.
    StyleProfile slouchOnly(double slouch)
    {
        StyleProfile p;
        p.name = "slouch_only";
        p.posture.slouchRad = slouch;
        p.idle.breathingAmplitudeM = 0.0;
        p.idle.swayAmplitudeM = 0.0;
        p.idle.fidgetIntervalS = 0.0;
        return p;
    }

    // Plays one source in one group name for the given number of frames.
    void play(Scene& scene, const Source& source, std::string_view group, int frames, float startTime = 0.f)
    {
        for (int f = 0; f < frames; ++f)
            scene.driver.update(sDt, allGroups(source, group, startTime + f * sDt), sTransition);
    }
}

// --- Off means unchanged ----------------------------------------------------------------------------------------

TEST(CreativeMotionStyleDriverTest, NeverGivenAStyleEqualsGivenButDisabled)
{
    const Source idle(0.f), walk(1.f);
    Scene plain, disabled;
    const StyleProfile profile = slouchOnly(0.4);
    disabled.driver.setStyle(&profile, 1234);
    disabled.driver.setStyleEnabled(false);

    const auto a = scriptedSequence(plain, idle, walk);
    const auto b = scriptedSequence(disabled, idle, walk, true); // the actor state is read only when enabled
    ASSERT_EQ(a.size(), b.size());
    for (size_t f = 0; f < a.size(); ++f)
        EXPECT_EQ(a[f], b[f]) << "frame " << f;
}

TEST(CreativeMotionStyleDriverTest, EnabledWithoutAProfileEqualsPlain)
{
    const Source idle(0.f), walk(1.f);
    Scene plain, enabledNoProfile;
    enabledNoProfile.driver.setStyle(nullptr, 5);
    enabledNoProfile.driver.setStyleEnabled(true);

    const auto a = scriptedSequence(plain, idle, walk);
    const auto b = scriptedSequence(enabledNoProfile, idle, walk, true);
    for (size_t f = 0; f < a.size(); ++f)
        EXPECT_EQ(a[f], b[f]) << "frame " << f;
}

TEST(CreativeMotionStyleDriverTest, DefaultsAreOff)
{
    Scene scene;
    const auto status = scene.driver.getStyleStatus();
    EXPECT_EQ(status.mProfile, nullptr);
    EXPECT_FALSE(status.mEnabled);
    EXPECT_NEAR(status.mCadence, 1.f, 1e-6f);
}

// --- Posture ----------------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleDriverTest, HeavyPostureChangesTheChestByHalfTheSlouch)
{
    const Source idle(0.f);
    const StyleProfile profile = slouchOnly(0.4);
    Scene styled, reference;
    styled.driver.setStyle(&profile, 77);
    styled.driver.setStyleEnabled(true);

    play(styled, idle, "idle", 90);
    play(reference, idle, "idle", 90);

    const float chest = quatAngle(styled.rotation("bip01 spine1"), reference.rotation("bip01 spine1"));
    EXPECT_NEAR(chest, 0.2f, 0.2f * 0.2f);

    // The spine takes part too, and is never turned further than the whole slouch.
    const float spine = quatAngle(styled.rotation("bip01 spine"), reference.rotation("bip01 spine"));
    EXPECT_GT(spine, 0.01f);
    EXPECT_LE(spine, 0.4f + 1e-3f);
}

TEST(CreativeMotionStyleDriverTest, UnmappedBonesAreLeftAlone)
{
    const Source idle(1.f);
    const StyleProfile profile = slouchOnly(0.4);
    Scene styled, reference;
    styled.driver.setStyle(&profile, 77);
    styled.driver.setStyleEnabled(true);

    play(styled, idle, "idle", 90);
    play(reference, idle, "idle", 90);

    for (const char* name : { "bip01", "bip01 tail" })
    {
        EXPECT_LT(distance(styled.translation(name), reference.translation(name)), 1e-5f) << name;
        // Compare the components, not an angle: acos of a float dot product near 1 cannot resolve better than about
        // 7e-4 rad (the dot of a unit quaternion with itself may come out 0.99999994 with fused multiply-add, as on
        // GCC), so an angle bound of 1e-4 would fail for two identical rotations.
        expectQuatNear(styled.rotation(name), reference.rotation(name), 1e-6f);
    }
}

TEST(CreativeMotionStyleDriverTest, PostureOffsetIsTheSameWhateverTheClipIs)
{
    // The style is on top of the sampled pose: a clip with rotations still gives a chest turned by about half the
    // slouch relative to the unstyled output.
    const Source walk(1.f);
    const StyleProfile profile = slouchOnly(0.4);
    Scene styled, reference;
    styled.driver.setStyle(&profile, 3);
    styled.driver.setStyleEnabled(true);
    play(styled, walk, "walkforward", 90);
    play(reference, walk, "walkforward", 90);

    const float chest = quatAngle(styled.rotation("bip01 spine1"), reference.rotation("bip01 spine1"));
    EXPECT_NEAR(chest, 0.2f, 0.2f * 0.2f);
}

// --- Switching the style on and off blends ----------------------------------------------------------------------

TEST(CreativeMotionStyleDriverTest, DisablingReturnsToTheClipPose)
{
    const Source idle(0.f);
    const StyleProfile profile = slouchOnly(0.4);
    Scene styled, reference;
    styled.driver.setStyle(&profile, 9);
    styled.driver.setStyleEnabled(true);
    play(styled, idle, "idle", 60);
    play(reference, idle, "idle", 60);
    ASSERT_GT(quatAngle(styled.rotation("bip01 spine1"), reference.rotation("bip01 spine1")), 0.1f);

    styled.driver.setStyleEnabled(false);
    play(styled, idle, "idle", 45, 1.f); // 0.75 s, well past the 0.25 s transition
    play(reference, idle, "idle", 45, 1.f);

    for (size_t i = 0; i < styled.driver.getSkeleton().bones.size(); ++i)
    {
        EXPECT_LT(distance(styled.driver.getBoneTranslation(i), reference.driver.getBoneTranslation(i)), 1e-4f) << i;
        EXPECT_LT(quatAngle(styled.driver.getBoneRotation(i), reference.driver.getBoneRotation(i)), 1e-4f) << i;
    }
}

TEST(CreativeMotionStyleDriverTest, TogglingOnBlendsInsteadOfJumping)
{
    const Source idle(0.f);
    const StyleProfile profile = slouchOnly(0.4);

    // The full offset, from a driver that has had the style on for a long time.
    Scene full, reference;
    full.driver.setStyle(&profile, 9);
    full.driver.setStyleEnabled(true);
    play(full, idle, "idle", 90);
    play(reference, idle, "idle", 90);
    const float fullOffset = quatAngle(full.rotation("bip01 spine1"), reference.rotation("bip01 spine1"));
    ASSERT_GT(fullOffset, 0.1f);

    Scene toggled;
    toggled.driver.setStyle(&profile, 9);
    toggled.driver.setStyleEnabled(false);
    play(toggled, idle, "idle", 30);
    const vsg::quat before = toggled.rotation("bip01 spine1");

    toggled.driver.setStyleEnabled(true);
    toggled.driver.update(sDt, allGroups(idle, "idle", 30 * sDt), sTransition);
    const float firstStyledStep = quatAngle(toggled.rotation("bip01 spine1"), before);
    EXPECT_LT(firstStyledStep, 0.25f * fullOffset);

    // It does arrive at the full offset after the transition time.
    play(toggled, idle, "idle", 45, 31 * sDt);
    EXPECT_NEAR(
        quatAngle(toggled.rotation("bip01 spine1"), reference.rotation("bip01 spine1")), fullOffset, 0.1f * fullOffset);
}

TEST(CreativeMotionStyleDriverTest, ChangingTheProfileBlends)
{
    const Source idle(0.f);
    const StyleProfile a = slouchOnly(0.1);
    const StyleProfile b = slouchOnly(0.5);
    Scene scene, reference;
    scene.driver.setStyle(&a, 4);
    scene.driver.setStyleEnabled(true);
    play(scene, idle, "idle", 60);
    play(reference, idle, "idle", 60);
    const float offsetA = quatAngle(scene.rotation("bip01 spine1"), reference.rotation("bip01 spine1"));
    const vsg::quat before = scene.rotation("bip01 spine1");

    scene.driver.setStyle(&b, 4);
    scene.driver.update(sDt, allGroups(idle, "idle", 1.f), sTransition);
    const float step = quatAngle(scene.rotation("bip01 spine1"), before);
    const float offsetB = 0.5f * 0.5f; // about half of 0.5
    EXPECT_LT(step, 0.25f * (offsetB - offsetA) + 1e-3f);
}

// --- Cadence ----------------------------------------------------------------------------------------------------

namespace
{
    StyleProfile slowProfile()
    {
        StyleProfile p = slouchOnly(0.1);
        p.name = "slow";
        p.gait.cadenceScale = 0.6;
        return p;
    }

    float cadenceAt(float speedMps, bool enabled, const StyleProfile* profile)
    {
        const Source idle(0.f);
        Scene scene;
        scene.driver.setStyle(profile, 1);
        scene.driver.setStyleEnabled(enabled);
        ActorState state;
        state.mSpeedMps = speedMps;
        scene.driver.setActorState(state);
        scene.driver.update(sDt, allGroups(idle, "walkforward", 0.f), sTransition);
        return scene.driver.getStyleStatus().mCadence;
    }
}

TEST(CreativeMotionStyleDriverTest, CadenceFollowsSpeedAndCadenceScale)
{
    const StyleProfile profile = slowProfile();
    EXPECT_NEAR(cadenceAt(1.5f, true, &profile), 0.6f, 1e-4f);
    EXPECT_NEAR(cadenceAt(0.f, true, &profile), 1.0f, 1e-4f);
    EXPECT_NEAR(cadenceAt(0.5f, true, &profile), 0.8f, 1e-4f);
}

TEST(CreativeMotionStyleDriverTest, CadenceIsOneWhenDisabledOrWithoutProfile)
{
    const StyleProfile profile = slowProfile();
    EXPECT_NEAR(cadenceAt(1.5f, false, &profile), 1.f, 1e-6f);
    EXPECT_NEAR(cadenceAt(1.5f, true, nullptr), 1.f, 1e-6f);
    EXPECT_NEAR(cadenceAt(1.5f, false, nullptr), 1.f, 1e-6f);
}

TEST(CreativeMotionStyleDriverTest, StyleStatusReportsProfileAndEnabled)
{
    const Source idle(0.f);
    const StyleProfile profile = slowProfile();
    Scene scene;
    scene.driver.setStyle(&profile, 1);
    scene.driver.setStyleEnabled(true);
    ActorState state;
    state.mSpeedMps = 0.5f;
    scene.driver.setActorState(state);
    scene.driver.update(sDt, allGroups(idle, "walkforward", 0.f), sTransition);

    const auto status = scene.driver.getStyleStatus();
    EXPECT_EQ(status.mProfile, &profile);
    EXPECT_TRUE(status.mEnabled);
    EXPECT_NEAR(status.mWeights.mMoving, 0.5f, 1e-4f);
    EXPECT_NEAR(status.mWeights.mIdle, 0.5f, 1e-4f);
}

// --- Transition rules -------------------------------------------------------------------------------------------

namespace
{
    float durationAfterChange(bool rules, std::string_view from, std::string_view to)
    {
        const Source source(0.f);
        Scene scene;
        scene.driver.setTransitionRules(rules);
        play(scene, source, from, 5);
        scene.driver.update(sDt, allGroups(source, to, 0.f), sTransition);
        return scene.driver.getTransitionStatus(0).mDuration;
    }
}

TEST(CreativeMotionStyleDriverTest, TransitionRulesChooseTheDurationFromTheGroups)
{
    EXPECT_NEAR(durationAfterChange(true, "walkforward", "idle"),
        MWRender::transitionSeconds(MotionKind::Walk, MotionKind::Idle, sTransition), 1e-5f);
    EXPECT_NEAR(durationAfterChange(true, "walkforward", "idle"), 1.5f * sTransition, 1e-5f);

    EXPECT_NEAR(durationAfterChange(true, "idle", "walkforward"),
        MWRender::transitionSeconds(MotionKind::Idle, MotionKind::Walk, sTransition), 1e-5f);
    EXPECT_NEAR(durationAfterChange(true, "idle", "walkforward"), 1.0f * sTransition, 1e-5f);

    EXPECT_NEAR(durationAfterChange(true, "walkforward", "runforward"), 0.8f * sTransition, 1e-5f);
    EXPECT_NEAR(durationAfterChange(true, "idle", "hit1"), 0.3f * sTransition, 1e-5f);
}

TEST(CreativeMotionStyleDriverTest, TransitionRulesOffUseTheBaseDuration)
{
    EXPECT_NEAR(durationAfterChange(false, "walkforward", "idle"), sTransition, 1e-6f);
    EXPECT_NEAR(durationAfterChange(false, "idle", "walkforward"), sTransition, 1e-6f);
    EXPECT_NEAR(durationAfterChange(false, "idle", "hit1"), sTransition, 1e-6f);
}

TEST(CreativeMotionStyleDriverTest, TransitionRulesDefaultToOff)
{
    const Source source(0.f);
    Scene scene;
    play(scene, source, "walkforward", 5);
    scene.driver.update(sDt, allGroups(source, "idle", 0.f), sTransition);
    EXPECT_NEAR(scene.driver.getTransitionStatus(0).mDuration, sTransition, 1e-6f);
}

// --- Determinism ------------------------------------------------------------------------------------------------

namespace
{
    StyleProfile lively()
    {
        StyleProfile p;
        p.name = "lively";
        p.posture.slouchRad = 0.1;
        p.idle.breathingAmplitudeM = 0.02;
        p.idle.swayAmplitudeM = 0.02;
        p.idle.fidgetIntervalS = 4.0;
        p.idle.seed = 99;
        return p;
    }

    std::vector<Snapshot> livelySequence(const StyleProfile& profile, std::uint64_t actorSeed)
    {
        const Source idle(0.f);
        Scene scene;
        scene.driver.setStyle(&profile, actorSeed);
        scene.driver.setStyleEnabled(true);
        std::vector<Snapshot> frames;
        for (int f = 0; f < 240; ++f)
        {
            scene.driver.update(sDt, allGroups(idle, "idle", f * sDt), sTransition);
            frames.push_back(snapshot(scene.driver));
        }
        return frames;
    }
}

TEST(CreativeMotionStyleDriverTest, SameSeedAndInputsGiveIdenticalOutput)
{
    const StyleProfile profile = lively();
    EXPECT_EQ(livelySequence(profile, 31337), livelySequence(profile, 31337));
}

TEST(CreativeMotionStyleDriverTest, DifferentActorSeedGivesDifferentIdleNoise)
{
    const StyleProfile profile = lively();
    const auto a = livelySequence(profile, 1);
    const auto b = livelySequence(profile, 2);
    ASSERT_EQ(a.size(), b.size());
    bool differs = false;
    for (size_t f = 0; f < a.size(); ++f)
        differs = differs || a[f] != b[f];
    EXPECT_TRUE(differs);
}

TEST(CreativeMotionStyleDriverTest, IdleNoiseMovesTheBodyOverTime)
{
    const StyleProfile profile = lively();
    const auto frames = livelySequence(profile, 5);

    const Source idle(0.f);
    Scene reference;
    std::vector<Snapshot> plain;
    for (int f = 0; f < 240; ++f)
    {
        reference.driver.update(sDt, allGroups(idle, "idle", f * sDt), sTransition);
        plain.push_back(snapshot(reference.driver));
    }
    bool differs = false;
    for (size_t f = 0; f < frames.size(); ++f)
        differs = differs || frames[f] != plain[f];
    EXPECT_TRUE(differs);
}

// Not written: "update allocates nothing in steady state". Counting allocations needs a replacement of the global
// operator new, which would apply to the whole test binary; the spec's no-allocation rule is left to the reviewer
// and to the implementation's own preallocation (makeEffectiveProfile's same-layer-count guarantee is tested in
// test_creativemotion_style.cpp).
