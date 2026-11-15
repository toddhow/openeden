// Task 0170: the clip meta of a combat clip, made from the sidecar's own text (no game data): the project file lists
// a generator with its events, the motion file gives the animation's duration, and linkClip joins them by name.
#include "apps/openmw/mwrender/skyrimclipmeta.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{
    // The layout of animationdata/<project>.txt: a file list, then per clip a name, a number, "1 0 0", the event count
    // and the events (docs/research/0054-provenance.md F44 to F53). Values copied from defaultmale.txt's
    // 1HM_AttackRight.
    const std::string sProject
        = "1\r\n1\r\nDefaultMale.hkx\r\n"
          "1\r\n"
          "1HM_AttackRight\r\n11\r\n1\r\n0\r\n0\r\n4\r\n"
          "preHitFrame:0.233\r\nweaponSwing:0.333\r\nHitFrame:0.367\r\nattackStop:1.287\r\n\r\n";

    TEST(SkyrimClipMeta, aClipNoGeneratorLinksToHasItsLengthAndNoEvents)
    {
        HavokPack::AnimationProject project;
        HavokPack::MotionTable motion;
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, motion, "NoSuchClip", 2.5f);
        ASSERT_NE(meta, nullptr);
        EXPECT_FLOAT_EQ(meta->mDuration, 2.5f);
        EXPECT_FLOAT_EQ(meta->speed(), 1.f);
        EXPECT_TRUE(meta->mEvents.empty());
        EXPECT_FLOAT_EQ(meta->playbackDuration(), 2.5f);
    }

    TEST(SkyrimClipMeta, aLinkedGeneratorGivesItsEventsSortedAndItsSpeed)
    {
        HavokPack::AnimationProject project;
        HavokPack::SidecarClip clip;
        clip.mName = "1HM_AttackRight";
        clip.mPlaybackSpeed = 1.f;
        clip.mEvents = { { "HitFrame", 0.367f }, { "preHitFrame", 0.233f } };
        project.mClips.push_back(clip);
        HavokPack::MotionTable motion; // no motion block: the generator cannot be checked and is kept
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, motion, "1hm_attackright", 1.3f);
        ASSERT_EQ(meta->mEvents.size(), 2u);
        EXPECT_EQ(meta->mEvents[0].mName, "preHitFrame"); // sorted
        EXPECT_FLOAT_EQ(meta->mEvents[1].mTime, 0.367f);
    }

    TEST(SkyrimClipMeta, theSidecarTextOfTheSwingParsesToTheFourEvents)
    {
        HavokPack::AnimationProject project = HavokPack::readAnimationProject(sProject);
        HavokPack::MotionTable motion;
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, motion, "1HM_AttackRight", 1.3f);
        ASSERT_EQ(meta->mEvents.size(), 4u);
        EXPECT_EQ(meta->mEvents[0].mName, "preHitFrame");
        EXPECT_EQ(meta->mEvents[1].mName, "weaponSwing");
        EXPECT_EQ(meta->mEvents[2].mName, "HitFrame");
        EXPECT_EQ(meta->mEvents[3].mName, "attackStop");
        EXPECT_FLOAT_EQ(meta->mEvents[3].mTime, 1.287f);
    }
}
