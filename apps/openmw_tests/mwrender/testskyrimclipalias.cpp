// Task 0214: a clip file whose generator has another name in the sidecar still gets its events (2hw_attackright is
// played by `2HW_AttackRightNPC`, shd_blockbash by `Shd_Bash`), and a clip that already has a generator is unchanged.
#include "apps/openmw/mwrender/skyrimclipmeta.hpp"

#include <gtest/gtest.h>

namespace
{
    HavokPack::SidecarClip clipOf(const char* name, float hit)
    {
        HavokPack::SidecarClip clip;
        clip.mName = name;
        clip.mEvents = { { "HitFrame", hit } };
        return clip;
    }

    TEST(SkyrimClipAlias, aClipFindsTheNpcGeneratorWhenNothingHasItsOwnName)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(clipOf("2HW_AttackRightNPC", 0.8f));
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "2HW_AttackRight", 2.f);
        ASSERT_EQ(meta->mEvents.size(), 1u);
        EXPECT_FLOAT_EQ(meta->mEvents[0].mTime, 0.8f);
    }

    TEST(SkyrimClipAlias, theShieldBashUsesTheBashGenerator)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(clipOf("Shd_Bash", 0.099f));
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "SHD_BlockBash", 0.4f);
        ASSERT_EQ(meta->mEvents.size(), 1u);
        EXPECT_FLOAT_EQ(meta->mEvents[0].mTime, 0.099f);
    }

    TEST(SkyrimClipAlias, aClipWithItsOwnGeneratorKeepsExactlyItsEvents)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(clipOf("2HM_AttackRight", 0.2f));
        project.mClips.push_back(clipOf("2HM_AttackRightNPC", 0.9f));
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "2HM_AttackRight", 1.7f);
        ASSERT_EQ(meta->mEvents.size(), 1u);
        EXPECT_FLOAT_EQ(meta->mEvents[0].mTime, 0.2f);
    }

    TEST(SkyrimClipAlias, aClipNobodyPlaysStillHasNoEvents)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(clipOf("Other", 0.1f));
        EXPECT_TRUE(MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "2HW_AttackRight", 2.f)->mEvents.empty());
    }

    TEST(SkyrimClipAlias, aliasesNameTheKnownSidecarNames)
    {
        const auto shield = MWRender::SkyrimClipMeta::sidecarAliases("shd_blockbash");
        ASSERT_EQ(shield.size(), 2u);
        EXPECT_EQ(shield[0], "Shd_Bash");
        EXPECT_EQ(shield[1], "shd_blockbashNPC");
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("1HM_AttackRight").size(), 1u);
    }
}
