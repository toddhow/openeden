// Task 0245 (the player's first-person view): the pure rules of the first-person arms. No renderer, no file.
#include <apps/openmw/mwrender/skyrimfirstperson.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace
{
    using namespace MWRender::SkyrimFirstPerson;

    TEST(SkyrimFirstPersonNames, aPackClipNameIsLoweredAndNamesTheFirstPersonFile)
    {
        EXPECT_EQ(armsClipName("1HM_AttackRight"), "1hm_attackright");
        EXPECT_EQ(clipPath("1HM_AttackRight"), "meshes/actors/character/_1stperson/animations/1hm_attackright.hkx");
        EXPECT_EQ(clipPath("MRh_Release"), "meshes/actors/character/_1stperson/animations/mrh_release.hkx");
    }

    TEST(SkyrimFirstPersonNames, aNameThatIsNotAPlainClipNameIsRefused)
    {
        EXPECT_TRUE(armsClipName("").empty());
        EXPECT_TRUE(armsClipName("a/b").empty());
        EXPECT_TRUE(armsClipName("a\\b").empty());
        EXPECT_TRUE(armsClipName("..").empty());
        EXPECT_TRUE(armsClipName("mt_idle.hkx").empty());
        EXPECT_TRUE(armsClipName(std::string(kMaxClipName + 1, 'a')).empty());
        EXPECT_FALSE(armsClipName(std::string(kMaxClipName, 'a')).empty());
        EXPECT_TRUE(clipPath("../skeleton").empty());
    }

    TEST(SkyrimFirstPersonIdle, theIdleFollowsTheStanceAndTheWeapon)
    {
        EXPECT_EQ(idleClipFor(1, Stance::Sheathed), "mt_idle");
        EXPECT_EQ(idleClipFor(0, Stance::Sheathed), "mt_idle");
        EXPECT_EQ(idleClipFor(1, Stance::Weapon), "1hm_idle");
        EXPECT_EQ(idleClipFor(2, Stance::Weapon), "1hm_idle");
        EXPECT_EQ(idleClipFor(3, Stance::Weapon), "1hm_idle");
        EXPECT_EQ(idleClipFor(4, Stance::Weapon), "1hm_idle");
        EXPECT_EQ(idleClipFor(5, Stance::Weapon), "2hm_idle");
        EXPECT_EQ(idleClipFor(6, Stance::Weapon), "2hw_idle");
        EXPECT_EQ(idleClipFor(0, Stance::Weapon), "h2h_idle");
        EXPECT_EQ(idleClipFor(1, Stance::Spell), "mrh_idle");
    }

    float lengthOf(const std::string& name)
    {
        if (name == "swing")
            return 1.0f;
        if (name == "slow")
            return kNotReady;
        if (name == "bad")
            return 0.f;
        return 4.0f; // loops
    }

    TEST(SkyrimFirstPersonTimeline, theLoopRunsFromTheFirstPickAndRestartsWhenItChanges)
    {
        Timeline t;
        t.setIdle("mt_idle");
        auto f = t.pick(10.f, lengthOf);
        EXPECT_EQ(f.clip, "mt_idle");
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
        EXPECT_FALSE(f.once);
        f = t.pick(10.5f, lengthOf);
        EXPECT_NEAR(f.time, 0.5f, 1e-6f);
        t.setIdle("1hm_idle");
        f = t.pick(11.f, lengthOf);
        EXPECT_EQ(f.clip, "1hm_idle");
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
        f = t.pick(11.25f, lengthOf);
        EXPECT_NEAR(f.time, 0.25f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimeline, aSwingPlaysOnceThenTheLoopComesBackFromItsStart)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(0.f, lengthOf);
        t.playOnce("swing");
        auto f = t.pick(1.f, lengthOf);
        EXPECT_EQ(f.clip, "swing");
        EXPECT_TRUE(f.once);
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
        f = t.pick(1.5f, lengthOf);
        EXPECT_EQ(f.clip, "swing");
        EXPECT_NEAR(f.time, 0.5f, 1e-6f);
        f = t.pick(2.2f, lengthOf); // 1.2 s after the start: over
        EXPECT_EQ(f.clip, "mt_idle");
        EXPECT_FALSE(f.once);
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
        f = t.pick(2.7f, lengthOf);
        EXPECT_NEAR(f.time, 0.5f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimeline, aSecondRequestRestartsTheSwing)
    {
        Timeline t;
        t.setIdle("mt_idle");
        t.playOnce("swing");
        (void)t.pick(1.f, lengthOf);
        (void)t.pick(1.6f, lengthOf);
        t.playOnce("swing");
        const auto f = t.pick(1.7f, lengthOf);
        EXPECT_EQ(f.clip, "swing");
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimeline, aClipStillDecodingKeepsTheLoopUntilItIsReady)
    {
        Timeline t;
        t.setIdle("mt_idle");
        t.playOnce("slow");
        EXPECT_EQ(t.pick(1.f, lengthOf).clip, "mt_idle");
        EXPECT_EQ(t.pick(2.f, lengthOf).clip, "mt_idle");
        // once the clip is ready the swing starts from that moment
        bool ready = false;
        const auto durationLater = [&ready](const std::string&) { return ready ? 1.0f : kNotReady; };
        Timeline u;
        u.setIdle("mt_idle");
        u.playOnce("slow");
        EXPECT_EQ(u.pick(1.f, durationLater).clip, "mt_idle");
        ready = true;
        const auto f = u.pick(1.4f, durationLater);
        EXPECT_EQ(f.clip, "slow");
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimeline, aClipThatNeverDecodesIsGivenUpAfterTheTimeout)
    {
        Timeline t;
        t.setIdle("mt_idle");
        t.playOnce("slow");
        (void)t.pick(1.f, lengthOf);
        const auto f = t.pick(1.f + kLoadTimeoutSeconds + 0.1f, lengthOf);
        EXPECT_EQ(f.clip, "mt_idle");
        // and it is not tried again
        EXPECT_EQ(t.pick(10.f, lengthOf).clip, "mt_idle");
    }

    TEST(SkyrimFirstPersonTimeline, aClipThatFailedIsDroppedAtOnce)
    {
        Timeline t;
        t.setIdle("mt_idle");
        t.playOnce("bad");
        EXPECT_EQ(t.pick(1.f, lengthOf).clip, "mt_idle");
        EXPECT_EQ(t.pick(1.1f, lengthOf).clip, "mt_idle");
    }

    TEST(SkyrimFirstPersonView, withNoPitchTheArmsRootStandsOnTheFeet)
    {
        const vsg::vec3 feet(100.f, 200.f, 30.f);
        const vsg::vec3 camRest(5.f, 12.f, 110.f);
        for (const float yaw : { 0.f, 0.7f, -2.f, 3.f })
        {
            const vsg::quat orient(yaw, vsg::vec3(0.f, 0.f, 1.f));
            const vsg::vec3 root = rootTranslation(feet, yaw, orient, camRest);
            EXPECT_NEAR(root.x, feet.x, 1e-3f);
            EXPECT_NEAR(root.y, feet.y, 1e-3f);
            EXPECT_NEAR(root.z, feet.z, 1e-3f);
        }
    }

    TEST(SkyrimFirstPersonView, thePitchTurnsTheArmsAboutTheCameraNodeNotTheFeet)
    {
        const vsg::vec3 feet(100.f, 200.f, 30.f);
        const vsg::vec3 camRest(5.f, 12.f, 110.f);
        const float yaw = 0.7f;
        const vsg::quat yawOnly(yaw, vsg::vec3(0.f, 0.f, 1.f));
        const vsg::vec3 eyeBase = feet + yawOnly * camRest;
        for (const float pitch : { -1.2f, -0.3f, 0.4f, 1.3f })
        {
            const vsg::quat orient = vsg::quat(pitch, vsg::vec3(1.f, 0.f, 0.f)) * yawOnly;
            const vsg::vec3 root = rootTranslation(feet, yaw, orient, camRest);
            const vsg::vec3 camera = root + orient * camRest;
            EXPECT_NEAR(camera.x, eyeBase.x, 1e-2f) << pitch;
            EXPECT_NEAR(camera.y, eyeBase.y, 1e-2f) << pitch;
            EXPECT_NEAR(camera.z, eyeBase.z, 1e-2f) << pitch;
        }
    }
}
