// Tests of the CreativeMotion style layer's pure parts (task 0053): group classification, transition times, style
// weights, the effective profile, style selection, the style library and the bone-mapped style layer. Written from
// the task spec's "Interfaces" section only, on a synthetic skeleton; no GPU, window, network or game data.

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/creativemotionstyle.hpp>

#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>

#include <opas/math/Quat.hpp>
#include <opas/math/Transform.hpp>
#include <opas/math/Vec3.hpp>
#include <opas/rig/Skeleton.hpp>
#include <opas/style/Noise.hpp>
#include <opas/style/StyleProfile.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef OPENMW_PROJECT_SOURCE_DIR
#define OPENMW_PROJECT_SOURCE_DIR "."
#endif

namespace
{
    using namespace MWRender;
    using opas::style::StyleProfile;

    constexpr float sBase = 0.25f;
    constexpr float sTol = 1e-5f;

    // A profile that does nothing: no posture, no idle life, no layers.
    StyleProfile quietProfile()
    {
        StyleProfile p;
        p.name = "quiet";
        p.idle.breathingAmplitudeM = 0.0;
        p.idle.swayAmplitudeM = 0.0;
        p.idle.fidgetIntervalS = 0.0;
        return p;
    }

    // A profile with every field different from the default, to see what is copied.
    StyleProfile busyProfile()
    {
        StyleProfile p;
        p.name = "busy";
        p.posture = { 0.2, 0.1, 0.3, 0.1, 0.05 };
        p.gait = { 0.9, 0.6, 0.7, 0.8, 0.02 };
        p.idle.breathingRateHz = 0.3;
        p.idle.breathingAmplitudeM = 0.02;
        p.idle.swayRateHz = 0.2;
        p.idle.swayAmplitudeM = 0.01;
        p.idle.fidgetIntervalS = 5.0;
        p.idle.seed = 42;
        p.timing = { 0.8, 1.2, 0.05 };
        p.gaze = { 0.4, 1.5, 0.7 };
        p.layers = { { "limp", 0.5, "" }, { "hunch", 0.4, "" }, { "protect_arm", 0.6, "LeftForearm" } };
        return p;
    }

    // --- The synthetic skeleton with Morrowind's normalized bone names ----------------------------------------------

    struct BoneDef
    {
        const char* name;
        int parent;
        opas::math::Vec3 translation;
    };

    const std::vector<BoneDef> sBones = {
        { "bip01 pelvis", -1, { 0.0, 0.0, 1.0 } }, // 0
        { "bip01 spine", 0, { 0.0, 0.0, 0.1 } }, // 1
        { "bip01 spine1", 1, { 0.0, 0.0, 0.15 } }, // 2
        { "bip01 neck", 2, { 0.0, 0.0, 0.25 } }, // 3
        { "bip01 head", 3, { 0.0, 0.0, 0.1 } }, // 4
        { "bip01 l clavicle", 2, { -0.1, 0.0, 0.2 } }, // 5
        { "bip01 l upperarm", 5, { -0.15, 0.0, 0.0 } }, // 6
        { "bip01 l forearm", 6, { -0.3, 0.0, 0.0 } }, // 7
        { "bip01 r clavicle", 2, { 0.1, 0.0, 0.2 } }, // 8
        { "bip01 r upperarm", 8, { 0.15, 0.0, 0.0 } }, // 9
        { "bip01 r forearm", 9, { 0.3, 0.0, 0.0 } }, // 10
        { "bip01 l thigh", 0, { -0.1, 0.0, -0.05 } }, // 11
        { "bip01 l calf", 11, { 0.0, 0.0, -0.45 } }, // 12
        { "bip01 r thigh", 0, { 0.1, 0.0, -0.05 } }, // 13
        { "bip01 r calf", 13, { 0.0, 0.0, -0.45 } }, // 14
        // Not styled: nothing in the style layer may touch these.
        { "bip01 tail", 0, { 0.0, -0.2, 0.0 } }, // 15
        { "bip01 l foot", 12, { 0.0, 0.0, -0.4 } }, // 16
    };
    constexpr int sPelvis = 0, sSpine = 1, sChest = 2, sTail = 15, sFoot = 16;
    constexpr int sMappedCount = 15;

    opas::rig::Skeleton makeSkeleton(const opas::math::Quat& pelvisRest = opas::math::Quat{})
    {
        opas::rig::Skeleton skeleton;
        for (const BoneDef& def : sBones)
        {
            opas::rig::Bone bone;
            bone.name = def.name;
            bone.parent = def.parent;
            bone.localRest.translation = def.translation;
            bone.localRest.rotation = opas::math::Quat{};
            skeleton.bones.push_back(bone);
        }
        skeleton.bones[sPelvis].localRest.rotation = pelvisRest;
        return skeleton;
    }

    std::vector<opas::math::Transform> restPose(const opas::rig::Skeleton& skeleton)
    {
        std::vector<opas::math::Transform> pose;
        for (const auto& bone : skeleton.bones)
            pose.push_back(bone.localRest);
        return pose;
    }

    opas::math::Vec3 worldUp(
        const opas::rig::Skeleton& skeleton, const std::vector<opas::math::Transform>& pose, int bone)
    {
        const auto world = opas::rig::computeWorldPose(skeleton, pose, opas::math::Transform{});
        return opas::math::rotate(world[static_cast<size_t>(bone)].rotation, opas::math::Vec3{ 0.0, 0.0, 1.0 });
    }

    bool samePose(const std::vector<opas::math::Transform>& a, const std::vector<opas::math::Transform>& b)
    {
        return a == b;
    }

    // The profile as a pose layer would see it with the default actor state, for the layer tests.
    void applyLayer(const opas::rig::Skeleton& skeleton, std::vector<opas::math::Transform>& pose,
        const StyleProfile& profile, double timeS)
    {
        CreativeMotionStyleLayer layer(skeleton);
        layer.apply(std::span<opas::math::Transform>(pose), profile, timeS);
    }
}

// --- Settings ---------------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, SettingsDefaults)
{
    EXPECT_FALSE(Settings::game().mCreativeMotionStyleLayer.get());
    EXPECT_FALSE(Settings::game().mCreativeMotionTransitionRules.get());
    EXPECT_EQ(Settings::game().mCreativeMotionDefaultStyle.get(), "cm_default");
}

// --- classifyGroup ----------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, ClassifyRealMorrowindGroups)
{
    const std::vector<std::pair<const char*, MotionKind>> expected = {
        { "idle", MotionKind::Idle },
        { "idle2", MotionKind::Idle },
        { "idlesneak", MotionKind::Sneak },
        { "idlehh", MotionKind::Ready },
        { "walkforward", MotionKind::Walk },
        { "walkleft", MotionKind::Strafe },
        { "runback", MotionKind::Run },
        { "runright", MotionKind::Strafe },
        { "sneakforward", MotionKind::Sneak },
        { "turnleft", MotionKind::Turn },
        { "turnright90", MotionKind::Turn },
        { "swimwalkforward", MotionKind::Swim },
        { "jump", MotionKind::Jump },
        { "weapononehand", MotionKind::Attack },
        { "handtohand", MotionKind::Attack },
        { "hit1", MotionKind::Hit },
        { "knockdown", MotionKind::Hit },
        { "death2", MotionKind::Death },
        { "shield", MotionKind::Ready },
        { "unknownthing", MotionKind::Other },
    };
    for (const auto& [name, kind] : expected)
        EXPECT_EQ(classifyGroup(name), kind) << name;
}

TEST(CreativeMotionStyleTest, ClassifySwimRules)
{
    EXPECT_EQ(classifyGroup("swim"), MotionKind::Swim);
    EXPECT_EQ(classifyGroup("swimwalkleft"), MotionKind::Swim); // swim wins over the strafe rule
    EXPECT_EQ(classifyGroup("swimrunright"), MotionKind::Swim);
    EXPECT_EQ(classifyGroup("swimidle"), MotionKind::Swim);
    EXPECT_EQ(classifyGroup("idleswim"), MotionKind::Swim);
}

TEST(CreativeMotionStyleTest, ClassifyIdleRules)
{
    EXPECT_EQ(classifyGroup("idle"), MotionKind::Idle);
    for (const char* name : { "idle1", "idle2", "idle3", "idle9", "idle10" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Idle) << name;
    EXPECT_EQ(classifyGroup("idlesneak"), MotionKind::Sneak);
    for (const char* name :
        { "idlehh", "idle1h", "idle2c", "idle2w", "idlecrossbow", "idlespell", "idlestorm", "idlefoo" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Ready) << name;
}

TEST(CreativeMotionStyleTest, ClassifyStrafeOnlyForWalkAndRun)
{
    EXPECT_EQ(classifyGroup("walkleft"), MotionKind::Strafe);
    EXPECT_EQ(classifyGroup("walkright"), MotionKind::Strafe);
    EXPECT_EQ(classifyGroup("runleft"), MotionKind::Strafe);
    EXPECT_EQ(classifyGroup("runright"), MotionKind::Strafe);
    EXPECT_EQ(classifyGroup("walkforward"), MotionKind::Walk);
    EXPECT_EQ(classifyGroup("walkback"), MotionKind::Walk);
    EXPECT_EQ(classifyGroup("runforward"), MotionKind::Run);
    EXPECT_EQ(classifyGroup("runback"), MotionKind::Run);
    // Sneak and turn are not strafes, whatever they end with.
    EXPECT_EQ(classifyGroup("sneakleft"), MotionKind::Sneak);
    EXPECT_EQ(classifyGroup("sneakright"), MotionKind::Sneak);
    EXPECT_EQ(classifyGroup("turnleft"), MotionKind::Turn);
    EXPECT_EQ(classifyGroup("turnright"), MotionKind::Turn);
}

TEST(CreativeMotionStyleTest, ClassifyExactNameRules)
{
    for (const char* name : { "weapononehand", "weapontwohand", "weapontwowide", "handtohand", "bowandarrow",
             "crossbow", "throwweapon", "spellcast" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Attack) << name;
    EXPECT_EQ(classifyGroup("shield"), MotionKind::Ready);
    EXPECT_EQ(classifyGroup("torch"), MotionKind::Ready);
    // Exact names only.
    EXPECT_EQ(classifyGroup("weapononehandx"), MotionKind::Other);
    EXPECT_EQ(classifyGroup("shieldx"), MotionKind::Other);
    EXPECT_EQ(classifyGroup("torchlight"), MotionKind::Other);
    EXPECT_EQ(classifyGroup("crossbowx"), MotionKind::Other);
}

TEST(CreativeMotionStyleTest, ClassifyPrefixRules)
{
    for (const char* name : { "hit1", "hit2", "hit5", "hitx", "knockdown", "knockout" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Hit) << name;
    for (const char* name : { "death1", "death5", "deathknockdown", "deathscripted" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Death) << name;
    for (const char* name : { "jump", "jump2", "jumpfoo" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Jump) << name;
    for (const char* name : { "turn", "turnleft", "turnright90" })
        EXPECT_EQ(classifyGroup(name), MotionKind::Turn) << name;
}

TEST(CreativeMotionStyleTest, ClassifyOther)
{
    EXPECT_EQ(classifyGroup(""), MotionKind::Other);
    EXPECT_EQ(classifyGroup("unknownthing"), MotionKind::Other);
    EXPECT_EQ(classifyGroup("container"), MotionKind::Other);
    EXPECT_EQ(classifyGroup("loot"), MotionKind::Other);
}

TEST(CreativeMotionStyleTest, ClassifyIsCaseInsensitive)
{
    EXPECT_EQ(classifyGroup("IDLE"), MotionKind::Idle);
    EXPECT_EQ(classifyGroup("Idle2"), MotionKind::Idle);
    EXPECT_EQ(classifyGroup("IdleSneak"), MotionKind::Sneak);
    EXPECT_EQ(classifyGroup("WalkForward"), MotionKind::Walk);
    EXPECT_EQ(classifyGroup("RUNLEFT"), MotionKind::Strafe);
    EXPECT_EQ(classifyGroup("WeaponOneHand"), MotionKind::Attack);
    EXPECT_EQ(classifyGroup("Death2"), MotionKind::Death);
    EXPECT_EQ(classifyGroup("SwimWalkForward"), MotionKind::Swim);
}

// --- transitionSeconds ------------------------------------------------------------------------------------------
// The factors below are multiples of the base time (the task's walk to idle is "1.5 x base").

TEST(CreativeMotionStyleTest, TransitionNonPositiveBaseGivesZero)
{
    for (MotionKind from : { MotionKind::Idle, MotionKind::Walk, MotionKind::Attack })
    {
        for (MotionKind to : { MotionKind::Idle, MotionKind::Hit, MotionKind::Death, MotionKind::Attack })
        {
            EXPECT_EQ(transitionSeconds(from, to, 0.f), 0.f);
            EXPECT_EQ(transitionSeconds(from, to, -1.f), 0.f);
        }
    }
}

TEST(CreativeMotionStyleTest, TransitionToHit)
{
    for (MotionKind from : { MotionKind::Idle, MotionKind::Walk, MotionKind::Attack, MotionKind::Strafe })
        EXPECT_NEAR(transitionSeconds(from, MotionKind::Hit, sBase), 0.3f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Hit, 0.4f), 0.12f, sTol);
}

TEST(CreativeMotionStyleTest, TransitionToDeathAndAttack)
{
    for (MotionKind from : { MotionKind::Idle, MotionKind::Walk, MotionKind::Attack, MotionKind::Strafe })
    {
        EXPECT_NEAR(transitionSeconds(from, MotionKind::Death, sBase), 0.5f * sBase, sTol);
        EXPECT_NEAR(transitionSeconds(from, MotionKind::Attack, sBase), 0.5f * sBase, sTol);
    }
}

TEST(CreativeMotionStyleTest, TransitionFromAttack)
{
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Idle, sBase), 1.2f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Walk, sBase), 1.2f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Run, sBase), 1.2f * sBase, sTol);
    // Precedence: "from Attack" beats "to or from Strafe".
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Strafe, sBase), 1.2f * sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionPrecedence)
{
    // To Hit beats from Attack; to Death and to Attack beat from Attack.
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Hit, sBase), 0.3f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Death, sBase), 0.5f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Attack, MotionKind::Attack, sBase), 0.5f * sBase, sTol);
    // To Attack beats to or from Strafe.
    EXPECT_NEAR(transitionSeconds(MotionKind::Strafe, MotionKind::Attack, sBase), 0.5f * sBase, sTol);
    // To Hit beats a stop.
    EXPECT_NEAR(transitionSeconds(MotionKind::Strafe, MotionKind::Hit, sBase), 0.3f * sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionStopping)
{
    for (MotionKind from : { MotionKind::Walk, MotionKind::Run, MotionKind::Sneak })
        EXPECT_NEAR(transitionSeconds(from, MotionKind::Idle, sBase), 1.5f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Walk, MotionKind::Idle, 0.4f), 0.6f, sTol);
    // Starting is the ordinary time.
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Walk, sBase), sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionWalkToRun)
{
    EXPECT_NEAR(transitionSeconds(MotionKind::Walk, MotionKind::Run, sBase), 0.8f * sBase, sTol);
    // Only that direction.
    EXPECT_NEAR(transitionSeconds(MotionKind::Run, MotionKind::Walk, sBase), sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionIdleAndTurn)
{
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Turn, sBase), 0.6f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Turn, MotionKind::Idle, sBase), 0.6f * sBase, sTol);
    // Not for Ready or Walk.
    EXPECT_NEAR(transitionSeconds(MotionKind::Ready, MotionKind::Turn, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Turn, MotionKind::Walk, sBase), sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionToOrFromStrafe)
{
    EXPECT_NEAR(transitionSeconds(MotionKind::Walk, MotionKind::Strafe, sBase), 0.8f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Strafe, MotionKind::Walk, sBase), 0.8f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Strafe, sBase), 0.8f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Strafe, MotionKind::Idle, sBase), 0.8f * sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Turn, MotionKind::Strafe, sBase), 0.8f * sBase, sTol);
}

TEST(CreativeMotionStyleTest, TransitionOtherwiseIsBase)
{
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Idle, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Ready, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Ready, MotionKind::Idle, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Jump, MotionKind::Idle, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Swim, MotionKind::Walk, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Other, MotionKind::Other, sBase), sBase, sTol);
    EXPECT_NEAR(transitionSeconds(MotionKind::Idle, MotionKind::Walk, 0.4f), 0.4f, sTol);
}

// --- styleWeights -----------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, WeightsStanding)
{
    const StyleWeights w = styleWeights(ActorState{});
    EXPECT_FLOAT_EQ(w.mMoving, 0.f);
    EXPECT_FLOAT_EQ(w.mIdle, 1.f);
    EXPECT_FLOAT_EQ(w.mPosture, 1.f);
}

TEST(CreativeMotionStyleTest, WeightsMovingIsSpeedClampedToOne)
{
    ActorState s;
    s.mSpeedMps = 0.5f;
    StyleWeights w = styleWeights(s);
    EXPECT_NEAR(w.mMoving, 0.5f, sTol);
    EXPECT_NEAR(w.mIdle, 0.5f, sTol);
    EXPECT_NEAR(w.mPosture, 1.f, sTol);

    s.mSpeedMps = 1.f;
    w = styleWeights(s);
    EXPECT_NEAR(w.mMoving, 1.f, sTol);
    EXPECT_NEAR(w.mIdle, 0.f, sTol);

    s.mSpeedMps = 3.f;
    w = styleWeights(s);
    EXPECT_NEAR(w.mMoving, 1.f, sTol);
    EXPECT_NEAR(w.mIdle, 0.f, sTol);

    s.mSpeedMps = -2.f;
    w = styleWeights(s);
    EXPECT_NEAR(w.mMoving, 0.f, sTol);
    EXPECT_NEAR(w.mIdle, 1.f, sTol);
}

TEST(CreativeMotionStyleTest, WeightsTurningOrHitSilenceIdle)
{
    ActorState s;
    s.mTurning = true;
    StyleWeights w = styleWeights(s);
    EXPECT_FLOAT_EQ(w.mIdle, 0.f);
    EXPECT_FLOAT_EQ(w.mMoving, 0.f);
    EXPECT_FLOAT_EQ(w.mPosture, 1.f);

    s = ActorState{};
    s.mHit = true;
    w = styleWeights(s);
    EXPECT_FLOAT_EQ(w.mIdle, 0.f);
    EXPECT_FLOAT_EQ(w.mPosture, 0.f);

    s = ActorState{};
    s.mTurning = true;
    s.mSpeedMps = 0.5f;
    w = styleWeights(s);
    EXPECT_FLOAT_EQ(w.mIdle, 0.f);
    EXPECT_NEAR(w.mMoving, 0.5f, sTol);
}

TEST(CreativeMotionStyleTest, WeightsPostureWithWeaponDrawn)
{
    ActorState s;
    s.mWeaponDrawn = true;
    EXPECT_NEAR(styleWeights(s).mPosture, 0.6f, sTol);
    EXPECT_FLOAT_EQ(styleWeights(s).mIdle, 1.f);

    s.mHit = true; // a hit wins over a drawn weapon
    EXPECT_FLOAT_EQ(styleWeights(s).mPosture, 0.f);
}

TEST(CreativeMotionStyleTest, WeightsRunningAndSneakingDoNotChangeWeights)
{
    ActorState s;
    s.mSpeedMps = 0.5f;
    const StyleWeights plain = styleWeights(s);
    s.mRunning = true;
    s.mSneaking = true;
    const StyleWeights flagged = styleWeights(s);
    EXPECT_FLOAT_EQ(plain.mMoving, flagged.mMoving);
    EXPECT_FLOAT_EQ(plain.mIdle, flagged.mIdle);
    EXPECT_FLOAT_EQ(plain.mPosture, flagged.mPosture);
}

// --- makeEffectiveProfile ---------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, EffectiveProfileScalesPosture)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 0.5f, 0.5f, 0.6f }, 99, out);

    EXPECT_NEAR(out.posture.slouchRad, 0.2 * 0.6, 1e-6);
    EXPECT_NEAR(out.posture.headPitchRad, 0.1 * 0.6, 1e-6);
    EXPECT_NEAR(out.posture.headYawRad, 0.3 * 0.6, 1e-6);
    EXPECT_NEAR(out.posture.shoulderElevationRad, 0.1 * 0.6, 1e-6);
    EXPECT_NEAR(out.posture.spineLeanRad, 0.05 * 0.6, 1e-6);
}

TEST(CreativeMotionStyleTest, EffectiveProfileCopiesNameGaitTimingGaze)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 0.2f, 0.3f, 0.1f }, 5, out);
    EXPECT_EQ(out.name, base.name);
    EXPECT_EQ(out.gait, base.gait);
    EXPECT_EQ(out.timing, base.timing);
    EXPECT_EQ(out.gaze, base.gaze);
}

TEST(CreativeMotionStyleTest, EffectiveProfileIdleScalesAmplitudesOnly)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 0.5f, 0.5f, 1.f }, 99, out);
    EXPECT_NEAR(out.idle.breathingAmplitudeM, 0.02 * 0.5, 1e-9);
    EXPECT_NEAR(out.idle.swayAmplitudeM, 0.01 * 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(out.idle.breathingRateHz, base.idle.breathingRateHz);
    EXPECT_DOUBLE_EQ(out.idle.swayRateHz, base.idle.swayRateHz);
    EXPECT_DOUBLE_EQ(out.idle.fidgetIntervalS, base.idle.fidgetIntervalS);
}

TEST(CreativeMotionStyleTest, EffectiveProfileIdleFadesOutBelowFivePercent)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 1.f, 0.04f, 1.f }, 99, out);
    EXPECT_EQ(out.idle.breathingAmplitudeM, 0.0);
    EXPECT_EQ(out.idle.swayAmplitudeM, 0.0);
    EXPECT_EQ(out.idle.fidgetIntervalS, 0.0);

    makeEffectiveProfile(base, StyleWeights{ 1.f, 0.f, 1.f }, 99, out);
    EXPECT_EQ(out.idle.breathingAmplitudeM, 0.0);
    EXPECT_EQ(out.idle.swayAmplitudeM, 0.0);
    EXPECT_EQ(out.idle.fidgetIntervalS, 0.0);

    makeEffectiveProfile(base, StyleWeights{ 1.f, 0.06f, 1.f }, 99, out);
    EXPECT_NEAR(out.idle.breathingAmplitudeM, 0.02 * 0.06, 1e-7);
    EXPECT_NEAR(out.idle.swayAmplitudeM, 0.01 * 0.06, 1e-7);
    EXPECT_DOUBLE_EQ(out.idle.fidgetIntervalS, base.idle.fidgetIntervalS);
}

TEST(CreativeMotionStyleTest, EffectiveProfileSeedIsBaseXorHashOfActorSeed)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    for (std::uint64_t actorSeed : { std::uint64_t{ 0 }, std::uint64_t{ 1 }, std::uint64_t{ 99 }, ~std::uint64_t{ 0 } })
    {
        makeEffectiveProfile(base, StyleWeights{}, actorSeed, out);
        EXPECT_EQ(out.idle.seed, base.idle.seed ^ opas::style::hash64(actorSeed)) << actorSeed;
    }
}

TEST(CreativeMotionStyleTest, EffectiveProfileLayerWeights)
{
    const StyleProfile base = busyProfile();
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 0.5f, 1.f, 1.f }, 1, out);
    ASSERT_EQ(out.layers.size(), 3u);
    EXPECT_EQ(out.layers[0].name, "limp");
    EXPECT_NEAR(out.layers[0].weight, 0.5 * 0.5, 1e-6); // limp scales with moving
    EXPECT_EQ(out.layers[1].name, "hunch");
    EXPECT_NEAR(out.layers[1].weight, 0.4, 1e-6); // the others by 1
    EXPECT_EQ(out.layers[2].name, "protect_arm");
    EXPECT_EQ(out.layers[2].bone, "LeftForearm");
    EXPECT_NEAR(out.layers[2].weight, 0.6, 1e-6);

    makeEffectiveProfile(base, StyleWeights{ 0.f, 1.f, 1.f }, 1, out);
    EXPECT_NEAR(out.layers[0].weight, 0.0, 1e-9);
    EXPECT_NEAR(out.layers[1].weight, 0.4, 1e-6);
}

TEST(CreativeMotionStyleTest, EffectiveProfileLayerWeightsAreClamped)
{
    StyleProfile base;
    base.layers = { { "limp", 2.0, "" }, { "hunch", 1.7, "" }, { "shiver", -0.5, "" } };
    StyleProfile out;
    makeEffectiveProfile(base, StyleWeights{ 1.f, 1.f, 1.f }, 1, out);
    ASSERT_EQ(out.layers.size(), 3u);
    for (const auto& layer : out.layers)
    {
        EXPECT_GE(layer.weight, 0.0) << layer.name;
        EXPECT_LE(layer.weight, 1.0) << layer.name;
    }
    EXPECT_DOUBLE_EQ(out.layers[0].weight, 1.0);
    EXPECT_DOUBLE_EQ(out.layers[1].weight, 1.0);
    EXPECT_DOUBLE_EQ(out.layers[2].weight, 0.0);
}

TEST(CreativeMotionStyleTest, EffectiveProfileIsDeterministicAndOverwritesOut)
{
    const StyleProfile base = busyProfile();
    const StyleWeights w{ 0.3f, 0.7f, 0.6f };
    StyleProfile a, b;
    makeEffectiveProfile(base, w, 7, a);
    makeEffectiveProfile(base, w, 7, b);
    EXPECT_EQ(a, b);

    // An out that held something else (more layers, other name) is fully replaced.
    StyleProfile c;
    c.name = "stale";
    c.layers = { { "shiver", 1.0, "x" }, { "limp", 1.0, "" }, { "hunch", 1.0, "" }, { "limp", 1.0, "" } };
    makeEffectiveProfile(base, w, 7, c);
    EXPECT_EQ(a, c);

    // Calling again with the same number of layers must not reallocate them.
    const auto* data = c.layers.data();
    makeEffectiveProfile(base, StyleWeights{ 1.f, 1.f, 1.f }, 8, c);
    EXPECT_EQ(c.layers.data(), data);
    EXPECT_EQ(c.layers.size(), base.layers.size());
}

// --- cadenceFactor ----------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, CadenceFactorInterpolatesByMoving)
{
    StyleProfile base;
    base.gait.cadenceScale = 0.6;
    EXPECT_NEAR(cadenceFactor(base, StyleWeights{ 0.f, 1.f, 1.f }), 1.f, sTol);
    EXPECT_NEAR(cadenceFactor(base, StyleWeights{ 0.5f, 0.5f, 1.f }), 0.8f, sTol);
    EXPECT_NEAR(cadenceFactor(base, StyleWeights{ 1.f, 0.f, 1.f }), 0.6f, sTol);

    base.gait.cadenceScale = 1.5;
    EXPECT_NEAR(cadenceFactor(base, StyleWeights{ 0.5f, 0.5f, 1.f }), 1.25f, sTol);

    base.gait.cadenceScale = 1.0;
    EXPECT_NEAR(cadenceFactor(base, StyleWeights{ 0.7f, 0.3f, 1.f }), 1.f, sTol);
}

// --- Style selection --------------------------------------------------------------------------------------------

TEST(CreativeMotionStyleTest, MakeStyleRulesLowerCasesKeysAndIgnoresUnknown)
{
    const StyleRules rules = makeStyleRules("cm_default",
        { { "actor.Guard_01", "StyleA" }, { "race.Nord", "StyleB" }, { "unknown.key", "StyleC" },
            { "plainkey", "StyleD" }, { "default style", "StyleE" } });
    EXPECT_EQ(rules.mDefaultStyle, "cm_default");
    ASSERT_EQ(rules.mActors.size(), 1u);
    ASSERT_EQ(rules.mRaces.size(), 1u);
    EXPECT_EQ(rules.mActors.begin()->first, "guard_01");
    EXPECT_EQ(rules.mActors.begin()->second, "StyleA");
    EXPECT_EQ(rules.mRaces.begin()->first, "nord");
    EXPECT_EQ(rules.mRaces.begin()->second, "StyleB");
}

TEST(CreativeMotionStyleTest, SelectActorBeatsRaceBeatsDefault)
{
    const StyleRules rules = makeStyleRules(
        "the_default", { { "actor.guard", "ActorStyle" }, { "race.nord", "RaceStyle" }, { "race.argonian", "Arg" } });

    EXPECT_EQ(selectStyleName(rules, "guard", "nord"), "ActorStyle");
    EXPECT_EQ(selectStyleName(rules, "guard", "argonian"), "ActorStyle");
    EXPECT_EQ(selectStyleName(rules, "someone", "nord"), "RaceStyle");
    EXPECT_EQ(selectStyleName(rules, "someone", "argonian"), "Arg");
    EXPECT_EQ(selectStyleName(rules, "someone", "breton"), "the_default");
    EXPECT_EQ(selectStyleName(rules, "", ""), "the_default");
}

TEST(CreativeMotionStyleTest, SelectIsCaseInsensitive)
{
    const StyleRules rules = makeStyleRules("d", { { "actor.Guard_01", "A" }, { "race.NORD", "R" } });
    EXPECT_EQ(selectStyleName(rules, "GUARD_01", "x"), "A");
    EXPECT_EQ(selectStyleName(rules, "guard_01", "x"), "A");
    EXPECT_EQ(selectStyleName(rules, "other", "Nord"), "R");
    EXPECT_EQ(selectStyleName(rules, "other", "nord"), "R");
}

TEST(CreativeMotionStyleTest, SelectIgnoresUnknownKeys)
{
    const StyleRules rules
        = makeStyleRules("d", { { "unknown.guard", "X" }, { "guard", "Y" }, { "class.guard", "Z" } });
    EXPECT_EQ(selectStyleName(rules, "guard", "nord"), "d");
}

TEST(CreativeMotionStyleTest, SelectWithNoEntriesGivesDefault)
{
    const StyleRules rules = makeStyleRules("only_default", {});
    EXPECT_EQ(selectStyleName(rules, "a", "b"), "only_default");
}

namespace
{
    // Restores the settings maps a test changes.
    struct SettingsMapsGuard
    {
        decltype(Settings::Manager::mUserSettings) mUser = Settings::Manager::mUserSettings;
        decltype(Settings::Manager::mDefaultSettings) mDefault = Settings::Manager::mDefaultSettings;
        ~SettingsMapsGuard()
        {
            Settings::Manager::mUserSettings = mUser;
            Settings::Manager::mDefaultSettings = mDefault;
        }
    };
}

TEST(CreativeMotionStyleTest, LoadRulesFromSettingsReadsTheStylesCategory)
{
    SettingsMapsGuard guard;
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Styles", "actor.TestGuard")] = "UserActor";
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Styles", "race.TestNord")] = "UserRace";
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Styles", "bogus.thing")] = "Bogus";

    const StyleRules rules = loadStyleRulesFromSettings();
    EXPECT_EQ(rules.mDefaultStyle, Settings::game().mCreativeMotionDefaultStyle.get());
    EXPECT_EQ(selectStyleName(rules, "testguard", "x"), "UserActor");
    EXPECT_EQ(selectStyleName(rules, "someone", "TESTNORD"), "UserRace");
    EXPECT_EQ(selectStyleName(rules, "someone", "other"), rules.mDefaultStyle);
}

TEST(CreativeMotionStyleTest, LoadRulesUserEntriesOverrideDefaultEntries)
{
    SettingsMapsGuard guard;
    Settings::Manager::mDefaultSettings[std::make_pair("CreativeMotion Styles", "actor.testboth")] = "FromDefault";
    Settings::Manager::mDefaultSettings[std::make_pair("CreativeMotion Styles", "actor.testonlydefault")]
        = "OnlyDefault";
    Settings::Manager::mUserSettings[std::make_pair("CreativeMotion Styles", "actor.testboth")] = "FromUser";

    const StyleRules rules = loadStyleRulesFromSettings();
    EXPECT_EQ(selectStyleName(rules, "testboth", "x"), "FromUser");
    EXPECT_EQ(selectStyleName(rules, "testonlydefault", "x"), "OnlyDefault");
}

// --- StyleLibrary -----------------------------------------------------------------------------------------------

namespace
{
    struct FakeFiles
    {
        std::map<std::string, std::string> mFiles;
        std::mutex mMutex;
        std::vector<std::string> mCalls;

        StyleLibrary::Reader reader()
        {
            return [this](const std::string& path) -> std::optional<std::string> {
                std::lock_guard lock(mMutex);
                mCalls.push_back(path);
                const auto it = mFiles.find(path);
                if (it == mFiles.end())
                    return std::nullopt;
                return it->second;
            };
        }

        int calls(const std::string& path)
        {
            std::lock_guard lock(mMutex);
            return static_cast<int>(std::count(mCalls.begin(), mCalls.end(), path));
        }
    };

    StyleProfile canonicalProfile(const std::string& name)
    {
        StyleProfile p;
        p.name = name;
        p.posture.slouchRad = 0.25;
        p.posture.headPitchRad = 0.125;
        p.gait.cadenceScale = 0.75;
        p.gait.strideScale = 0.5;
        p.idle.breathingAmplitudeM = 0.02;
        p.idle.fidgetIntervalS = 4.0;
        p.idle.seed = 12345;
        p.layers = { { "hunch", 0.5, "" }, { "limp", 0.25, "" } };
        return p;
    }
}

TEST(CreativeMotionStyleTest, CanonicalProfileRoundTripsThroughWriterAndLoader)
{
    const StyleProfile p = canonicalProfile("round_trip");
    const std::string text = opas::style::writeStyleProfile(p);
    const auto loaded = opas::style::loadStyleProfile(text, "round_trip.style.json");
    ASSERT_TRUE(loaded.ok()) << loaded.error.toString();
    EXPECT_EQ(*loaded.value, p);
    EXPECT_EQ(opas::style::writeStyleProfile(*loaded.value), text);
}

TEST(CreativeMotionStyleTest, LibraryLoadsFromLowerCasePathAndCaches)
{
    FakeFiles files;
    const StyleProfile p = canonicalProfile("cm_test");
    files.mFiles["styles/cm_test.style.json"] = opas::style::writeStyleProfile(p);
    StyleLibrary library(files.reader());

    const StyleProfile* first = library.find("cm_test");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(*first, p);
    EXPECT_EQ(files.calls("styles/cm_test.style.json"), 1);

    EXPECT_EQ(library.find("cm_test"), first);
    EXPECT_EQ(files.calls("styles/cm_test.style.json"), 1);
}

TEST(CreativeMotionStyleTest, LibraryPathUsesLowerCaseName)
{
    FakeFiles files;
    files.mFiles["styles/cm_mixed.style.json"] = opas::style::writeStyleProfile(canonicalProfile("cm_mixed"));
    StyleLibrary library(files.reader());

    const StyleProfile* found = library.find("CM_Mixed");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "cm_mixed");
    ASSERT_FALSE(files.mCalls.empty());
    EXPECT_EQ(files.mCalls.front(), "styles/cm_mixed.style.json");
}

TEST(CreativeMotionStyleTest, LibraryMissingFileGivesNullptrAndReadsOnce)
{
    FakeFiles files;
    StyleLibrary library(files.reader());
    EXPECT_EQ(library.find("nothing_here"), nullptr);
    EXPECT_EQ(library.find("nothing_here"), nullptr);
    EXPECT_EQ(files.calls("styles/nothing_here.style.json"), 1);
}

TEST(CreativeMotionStyleTest, LibraryInvalidFileGivesNullptrAndReadsOnce)
{
    FakeFiles files;
    files.mFiles["styles/broken.style.json"] = "{ this is not json";
    files.mFiles["styles/wrongformat.style.json"] = "{\"format\":\"something.else\",\"version\":1,\"name\":\"x\"}";
    StyleLibrary library(files.reader());
    EXPECT_EQ(library.find("broken"), nullptr);
    EXPECT_EQ(library.find("broken"), nullptr);
    EXPECT_EQ(files.calls("styles/broken.style.json"), 1);
    EXPECT_EQ(library.find("wrongformat"), nullptr);
}

TEST(CreativeMotionStyleTest, LibraryEmptyNameGivesNullptrWithoutReading)
{
    FakeFiles files;
    StyleLibrary library(files.reader());
    EXPECT_EQ(library.find(""), nullptr);
    EXPECT_TRUE(files.mCalls.empty());
}

TEST(CreativeMotionStyleTest, LibraryPointersStayValidAndDistinct)
{
    FakeFiles files;
    for (int i = 0; i < 50; ++i)
    {
        const std::string name = "style_" + std::to_string(i);
        StyleProfile p = canonicalProfile(name);
        p.posture.slouchRad = 0.01 * i;
        files.mFiles["styles/" + name + ".style.json"] = opas::style::writeStyleProfile(p);
    }
    StyleLibrary library(files.reader());

    const StyleProfile* first = library.find("style_0");
    ASSERT_NE(first, nullptr);
    std::vector<const StyleProfile*> all;
    for (int i = 0; i < 50; ++i)
    {
        const StyleProfile* p = library.find("style_" + std::to_string(i));
        ASSERT_NE(p, nullptr);
        all.push_back(p);
    }
    EXPECT_EQ(all[0], first);
    for (int i = 0; i < 50; ++i)
    {
        EXPECT_EQ(all[i]->name, "style_" + std::to_string(i));
        EXPECT_NEAR(all[i]->posture.slouchRad, 0.01 * i, 1e-9);
        for (int j = i + 1; j < 50; ++j)
            EXPECT_NE(all[i], all[j]);
    }
}

TEST(CreativeMotionStyleTest, LibraryIsThreadSafe)
{
    FakeFiles files;
    files.mFiles["styles/shared.style.json"] = opas::style::writeStyleProfile(canonicalProfile("shared"));
    StyleLibrary library(files.reader());

    constexpr int sThreads = 8;
    std::vector<const StyleProfile*> results(sThreads, nullptr);
    std::vector<std::thread> threads;
    for (int t = 0; t < sThreads; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 100; ++i)
                results[t] = library.find("shared");
        });
    }
    for (auto& thread : threads)
        thread.join();

    ASSERT_NE(results[0], nullptr);
    for (const StyleProfile* r : results)
        EXPECT_EQ(r, results[0]);
    EXPECT_EQ(files.calls("styles/shared.style.json"), 1);
}

// --- Shipped styles ---------------------------------------------------------------------------------------------

namespace
{
    std::filesystem::path shippedStyle(const char* file)
    {
        return std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data" / "styles" / file;
    }
}

TEST(CreativeMotionStyleTest, ShippedDefaultStyle)
{
    const auto loaded = opas::style::loadStyleProfileFile(shippedStyle("cm_default.style.json"));
    ASSERT_TRUE(loaded.ok()) << loaded.error.toString();
    EXPECT_EQ(loaded.value->name, "cm_default");

    // "Format defaults": every other field is the format's default.
    StyleProfile expected;
    expected.name = "cm_default";
    EXPECT_EQ(*loaded.value, expected);
}

TEST(CreativeMotionStyleTest, ShippedHeavyTestStyle)
{
    const auto loaded = opas::style::loadStyleProfileFile(shippedStyle("cm_heavy_test.style.json"));
    ASSERT_TRUE(loaded.ok()) << loaded.error.toString();
    const StyleProfile& p = *loaded.value;
    EXPECT_EQ(p.name, "cm_heavy_test");
    EXPECT_DOUBLE_EQ(p.posture.slouchRad, 0.35);
    EXPECT_DOUBLE_EQ(p.posture.headPitchRad, 0.2);
    EXPECT_DOUBLE_EQ(p.posture.shoulderElevationRad, -0.1);
    EXPECT_DOUBLE_EQ(p.gait.cadenceScale, 0.6);
    EXPECT_DOUBLE_EQ(p.gait.strideScale, 0.8);
    EXPECT_DOUBLE_EQ(p.idle.breathingAmplitudeM, 0.02);

    ASSERT_EQ(p.layers.size(), 2u);
    double hunch = -1.0, limp = -1.0;
    for (const auto& layer : p.layers)
    {
        if (layer.name == "hunch")
            hunch = layer.weight;
        else if (layer.name == "limp")
            limp = layer.weight;
    }
    EXPECT_DOUBLE_EQ(hunch, 0.5);
    EXPECT_DOUBLE_EQ(limp, 0.3);
}

// --- describeStyle ----------------------------------------------------------------------------------------------

namespace
{
    StyleProfile heavyLike()
    {
        StyleProfile p;
        p.name = "cm_heavy_test";
        p.posture.slouchRad = 0.35;
        p.gait.cadenceScale = 0.6;
        p.layers = { { "hunch", 0.5, "" }, { "limp", 0.3, "" } };
        return p;
    }
}

TEST(CreativeMotionStyleTest, DescribeStyleContainsTheParts)
{
    const std::string on = describeStyle(heavyLike(), true, StyleWeights{ 0.5f, 0.5f, 0.6f });
    EXPECT_NE(on.find("cm_heavy_test"), std::string::npos) << on;
    EXPECT_NE(on.find("[on]"), std::string::npos) << on;
    EXPECT_EQ(on.find("[off]"), std::string::npos) << on;
    EXPECT_NE(on.find("moving=0.50"), std::string::npos) << on;
    EXPECT_NE(on.find("idle=0.50"), std::string::npos) << on;
    EXPECT_NE(on.find("posture=0.60"), std::string::npos) << on;
    EXPECT_NE(on.find("cadence=0.80"), std::string::npos) << on;
    EXPECT_NE(on.find("slouch=0.350"), std::string::npos) << on;
    EXPECT_NE(on.find("hunch=0.50"), std::string::npos) << on;
    EXPECT_NE(on.find("limp=0.30"), std::string::npos) << on;
    EXPECT_EQ(on.find("layers: none"), std::string::npos) << on;

    const std::string off = describeStyle(heavyLike(), false, StyleWeights{});
    EXPECT_NE(off.find("[off]"), std::string::npos) << off;
    EXPECT_EQ(off.find("[on]"), std::string::npos) << off;
}

TEST(CreativeMotionStyleTest, DescribeStyleNoLayers)
{
    StyleProfile p;
    p.name = "plain";
    const std::string text = describeStyle(p, true, StyleWeights{});
    EXPECT_NE(text.find("layers: none"), std::string::npos) << text;
    EXPECT_NE(text.find("plain"), std::string::npos) << text;
}

TEST(CreativeMotionStyleTest, DescribeStyleLiteralFormat)
{
    // The spec's literal text, including two spaces after the on/off marker.
    EXPECT_EQ(describeStyle(heavyLike(), true, StyleWeights{ 0.5f, 0.5f, 0.6f }),
        "style cm_heavy_test [on]  moving=0.50 idle=0.50 posture=0.60 cadence=0.80 slouch=0.350 layers: hunch=0.50 "
        "limp=0.30");
}

// --- CreativeMotionStyleLayer -----------------------------------------------------------------------------------

TEST(CreativeMotionStyleLayerTest, BindsTheMappedBones)
{
    const auto skeleton = makeSkeleton();
    CreativeMotionStyleLayer layer(skeleton);
    EXPECT_EQ(layer.boundBones(), sMappedCount);
}

TEST(CreativeMotionStyleLayerTest, SkeletonWithoutTheNamesBindsNothingAndChangesNothing)
{
    opas::rig::Skeleton skeleton;
    for (const char* name : { "root", "a", "b", "tail" })
    {
        opas::rig::Bone bone;
        bone.name = name;
        bone.parent = skeleton.bones.empty() ? -1 : 0;
        bone.localRest.translation = { 0.0, 0.0, 0.1 * static_cast<double>(skeleton.bones.size()) };
        skeleton.bones.push_back(bone);
    }
    CreativeMotionStyleLayer layer(skeleton);
    EXPECT_EQ(layer.boundBones(), 0);

    StyleProfile p = busyProfile();
    p.posture.slouchRad = 0.4;
    auto pose = restPose(skeleton);
    const auto before = pose;
    layer.apply(std::span<opas::math::Transform>(pose), p, 1.3);
    EXPECT_TRUE(samePose(pose, before));
}

TEST(CreativeMotionStyleLayerTest, PartialSkeletonBindsOnlyWhatItHas)
{
    opas::rig::Skeleton skeleton = makeSkeleton();
    skeleton.bones.resize(3); // pelvis, spine, spine1
    CreativeMotionStyleLayer layer(skeleton);
    EXPECT_EQ(layer.boundBones(), 3);
}

TEST(CreativeMotionStyleLayerTest, ZeroEffectProfileLeavesThePoseBitIdentical)
{
    const auto skeleton = makeSkeleton();
    CreativeMotionStyleLayer layer(skeleton);
    auto pose = restPose(skeleton);
    // A non-trivial starting pose, so that "unchanged" means something.
    pose[sSpine].rotation = opas::math::fromAxisAngle({ 1.0, 0.0, 0.0 }, 0.1);
    pose[sChest].rotation = opas::math::fromAxisAngle({ 0.0, 1.0, 0.0 }, -0.2);
    const auto before = pose;

    for (double t : { 0.0, 0.37, 1.0, 12.5 })
    {
        layer.apply(std::span<opas::math::Transform>(pose), quietProfile(), t);
        EXPECT_TRUE(samePose(pose, before)) << t;
    }
}

TEST(CreativeMotionStyleLayerTest, PositiveSlouchLeansTowardPlusY)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = quietProfile();
    p.posture.slouchRad = 0.3;

    auto pose = restPose(skeleton);
    applyLayer(skeleton, pose, p, 0.0);

    for (int bone : { sSpine, sChest })
    {
        const auto up = worldUp(skeleton, pose, bone);
        EXPECT_GT(up.y, 1e-3) << bone;
        EXPECT_NEAR(up.x, 0.0, 1e-6) << bone;
        EXPECT_GT(up.z, 0.0) << bone;
    }
}

TEST(CreativeMotionStyleLayerTest, NegativeSlouchLeansTowardMinusY)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = quietProfile();
    p.posture.slouchRad = -0.3;

    auto pose = restPose(skeleton);
    applyLayer(skeleton, pose, p, 0.0);
    EXPECT_LT(worldUp(skeleton, pose, sChest).y, -1e-3);
}

TEST(CreativeMotionStyleLayerTest, LeanGrowsWithSlouch)
{
    const auto skeleton = makeSkeleton();
    double previous = 0.0;
    for (double slouch : { 0.1, 0.2, 0.4 })
    {
        StyleProfile p = quietProfile();
        p.posture.slouchRad = slouch;
        auto pose = restPose(skeleton);
        applyLayer(skeleton, pose, p, 0.0);
        const double lean = worldUp(skeleton, pose, sChest).y;
        EXPECT_GT(lean, previous) << slouch;
        previous = lean;
    }
}

TEST(CreativeMotionStyleLayerTest, OnlyMappedBonesChange)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = busyProfile();
    p.posture.slouchRad = 0.4;

    auto pose = restPose(skeleton);
    const auto before = pose;
    applyLayer(skeleton, pose, p, 1.7);

    EXPECT_EQ(pose[sTail], before[sTail]);
    EXPECT_EQ(pose[sFoot], before[sFoot]);
    EXPECT_FALSE(samePose(pose, before)); // the mapped bones did change
}

TEST(CreativeMotionStyleLayerTest, LeanTowardPlusYInModelSpaceWithRotatedRestPose)
{
    // The pelvis rest turned about Z: a lean in the bone's own frame would go sideways or backwards.
    for (double angle : { 1.5707963267948966, 3.141592653589793, -1.5707963267948966, 0.7 })
    {
        const auto skeleton = makeSkeleton(opas::math::fromAxisAngle({ 0.0, 0.0, 1.0 }, angle));
        StyleProfile p = quietProfile();
        p.posture.slouchRad = 0.3;

        auto pose = restPose(skeleton);
        applyLayer(skeleton, pose, p, 0.0);
        for (int bone : { sSpine, sChest })
        {
            const auto up = worldUp(skeleton, pose, bone);
            EXPECT_GT(up.y, 1e-3) << angle << " bone " << bone;
            EXPECT_NEAR(up.x, 0.0, 1e-6) << angle << " bone " << bone;
        }
    }
}

TEST(CreativeMotionStyleLayerTest, SameInputsSameOutputs)
{
    const auto skeleton = makeSkeleton();
    const StyleProfile p = busyProfile();
    CreativeMotionStyleLayer layer(skeleton);

    auto a = restPose(skeleton);
    auto b = restPose(skeleton);
    layer.apply(std::span<opas::math::Transform>(a), p, 2.2);
    layer.apply(std::span<opas::math::Transform>(b), p, 2.2);
    EXPECT_TRUE(samePose(a, b));

    // A second layer object over the same skeleton gives the same result.
    CreativeMotionStyleLayer other(skeleton);
    auto c = restPose(skeleton);
    other.apply(std::span<opas::math::Transform>(c), p, 2.2);
    EXPECT_TRUE(samePose(a, c));
}

TEST(CreativeMotionStyleLayerTest, IdleLifeMovesTheBodyAndFollowsTheSeed)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = quietProfile();
    p.idle.breathingAmplitudeM = 0.02;
    p.idle.swayAmplitudeM = 0.02;
    p.idle.seed = 1;

    bool movedAtSomeTime = false;
    bool seedMatteredAtSomeTime = false;
    for (double t : { 0.3, 0.9, 1.6, 2.4, 3.1, 4.7 })
    {
        auto pose = restPose(skeleton);
        applyLayer(skeleton, pose, p, t);
        if (!samePose(pose, restPose(skeleton)))
            movedAtSomeTime = true;

        StyleProfile q = p;
        q.idle.seed = 2;
        auto other = restPose(skeleton);
        applyLayer(skeleton, other, q, t);
        if (!samePose(pose, other))
            seedMatteredAtSomeTime = true;
    }
    EXPECT_TRUE(movedAtSomeTime);
    EXPECT_TRUE(seedMatteredAtSomeTime);
}

TEST(CreativeMotionStyleLayerTest, ZeroWeightLayersChangeNothing)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = quietProfile();
    p.layers = { { "limp", 0.0, "" }, { "hunch", 0.0, "" }, { "shiver", 0.0, "" } };
    auto pose = restPose(skeleton);
    const auto before = pose;
    applyLayer(skeleton, pose, p, 1.0);
    EXPECT_TRUE(samePose(pose, before));
}

TEST(CreativeMotionStyleLayerTest, HunchLayerChangesTheBody)
{
    const auto skeleton = makeSkeleton();
    StyleProfile p = quietProfile();
    p.layers = { { "hunch", 0.8, "" } };
    auto pose = restPose(skeleton);
    applyLayer(skeleton, pose, p, 1.0);
    EXPECT_FALSE(samePose(pose, restPose(skeleton)));
    EXPECT_EQ(pose[sTail], skeleton.bones[sTail].localRest);
}
