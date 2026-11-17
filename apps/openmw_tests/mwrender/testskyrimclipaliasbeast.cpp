// Task 0279: the werewolf's right power attack. The file `ww_attackright` (1.26667 s) has no generator of its own name
// in the sidecar (`werewolfbeastproject.txt`); the generator of that duration and of the same events is
// `AttackRightPower_State` (cache block 12, playback speed 1.1, HitFrame at 0.232 s), so the clip is linked by that
// alias. Every other `ww_` clip keeps the aliases it had.
#include "apps/openmw/mwrender/skyrimclipmeta.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    TEST(SkyrimClipAliasBeast, theRightPowerAttackNamesItsStateGenerator)
    {
        const auto aliases = MWRender::SkyrimClipMeta::sidecarAliases("ww_attackright");
        EXPECT_NE(std::find(aliases.begin(), aliases.end(), "AttackRightPower_State"), aliases.end());
        EXPECT_NE(std::find(aliases.begin(), aliases.end(), "ww attackright.hkx"), aliases.end())
            << "the old guesses are still tried first";
    }

    TEST(SkyrimClipAliasBeast, theRightPowerAttackGetsItsHitFrameFromTheStateGenerator)
    {
        HavokPack::AnimationProject project;
        HavokPack::SidecarClip clip;
        clip.mName = "AttackRightPower_State";
        clip.mPlaybackSpeed = 1.1f;
        clip.mEvents = { { "weaponSwing", 0.175f }, { "HitFrame", 0.232f } };
        project.mClips.push_back(clip);
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "ww_attackright", 1.26667f);
        ASSERT_EQ(meta->mEvents.size(), 2u);
        EXPECT_EQ(meta->mEvents[1].mName, "HitFrame");
        EXPECT_FLOAT_EQ(meta->mEvents[1].mTime, 0.232f);
        EXPECT_FLOAT_EQ(meta->mSpeed, 1.1f);
    }

    TEST(SkyrimClipAliasBeast, theOtherBeastClipsKeepTheirAliases)
    {
        const auto left = MWRender::SkyrimClipMeta::sidecarAliases("ww_attackleft");
        EXPECT_EQ(std::count(left.begin(), left.end(), "AttackRightPower_State"), 0);
        const auto fast = MWRender::SkyrimClipMeta::sidecarAliases("ww_attackrightfast");
        EXPECT_NE(std::find(fast.begin(), fast.end(), "ww attackrightfastlow.hkx"), fast.end());
        EXPECT_EQ(std::count(fast.begin(), fast.end(), "AttackRightPower_State"), 0);
    }
}
