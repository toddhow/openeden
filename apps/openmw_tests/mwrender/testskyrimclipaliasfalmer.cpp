// Task 0246: the falmer's two hand-to-hand attack files are played by generators with another name in the sidecar
// (`h2hattack1` by `H2H_Attack1_Clip`), so their HitFrame is found by an alias, and the aliases of every other clip are
// the ones they were.
#include "apps/openmw/mwrender/skyrimclipmeta.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    HavokPack::SidecarClip generatorOf(const char* name, float hit)
    {
        HavokPack::SidecarClip clip;
        clip.mName = name;
        clip.mEvents = { { "HitFrame", hit } };
        return clip;
    }

    TEST(SkyrimClipAliasFalmer, theFalmersAttacksNameTheirClipGenerators)
    {
        const auto first = MWRender::SkyrimClipMeta::sidecarAliases("h2hattack1");
        const auto second = MWRender::SkyrimClipMeta::sidecarAliases("H2HAttack2");
        EXPECT_NE(std::find(first.begin(), first.end(), "H2H_Attack1_Clip"), first.end());
        EXPECT_NE(std::find(second.begin(), second.end(), "H2H_Attack2_Clip"), second.end());
    }

    TEST(SkyrimClipAliasFalmer, theFalmersAttackGetsItsHitFrameFromTheClipGenerator)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(generatorOf("H2H_Attack1_Clip", 0.46f));
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "h2hattack1", 1.4667f);
        ASSERT_EQ(meta->mEvents.size(), 1u);
        EXPECT_EQ(meta->mEvents[0].mName, "HitFrame");
        EXPECT_FLOAT_EQ(meta->mEvents[0].mTime, 0.46f);
    }

    TEST(SkyrimClipAliasFalmer, otherClipsKeepTheAliasesTheyHad)
    {
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("h2hattackrighta").size(), 1u);
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("attack1").size(), 1u);
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("shd_blockbash").size(), 2u);
    }
}
