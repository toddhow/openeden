// Task 0301: the chaurus's spit is the file `attack_releasespit`, played by the generator `Attack_CastingA` of the
// sidecar, so the SpellFire event is found by an alias; every other clip keeps the aliases it had.
#include "apps/openmw/mwrender/skyrimclipmeta.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    HavokPack::SidecarClip castingGenerator()
    {
        HavokPack::SidecarClip clip;
        clip.mName = "Attack_CastingA";
        clip.mEvents = { { "MLh_SpellFire_Event", 0.1f }, { "StartAnimationDriven", 0.233f } };
        return clip;
    }

    TEST(SkyrimClipAliasCast, theChaurusSpitNamesItsCastingGenerator)
    {
        const auto aliases = MWRender::SkyrimClipMeta::sidecarAliases("attack_releasespit");
        EXPECT_NE(std::find(aliases.begin(), aliases.end(), "Attack_CastingA"), aliases.end());
        const auto upper = MWRender::SkyrimClipMeta::sidecarAliases("Attack_ReleaseSpit");
        EXPECT_NE(std::find(upper.begin(), upper.end(), "Attack_CastingA"), upper.end());
    }

    TEST(SkyrimClipAliasCast, theSpitGetsItsSpellFireEventFromTheCastingGenerator)
    {
        HavokPack::AnimationProject project;
        project.mClips.push_back(castingGenerator());
        const auto meta = MWRender::SkyrimClipMeta::makeClipMeta(project, {}, "attack_releasespit", 1.16667f);
        ASSERT_FALSE(meta->mEvents.empty());
        const auto found = std::find_if(meta->mEvents.begin(), meta->mEvents.end(),
            [](const auto& event) { return event.mName == "MLh_SpellFire_Event"; });
        ASSERT_NE(found, meta->mEvents.end());
        EXPECT_FLOAT_EQ(found->mTime, 0.1f);
    }

    TEST(SkyrimClipAliasCast, theSpiderAndThePriestClipsLinkByTheirOwnNamesAndOtherClipsKeepTheirAliases)
    {
        // Their files carry the generators' names, so they need no alias beyond the one every clip has.
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("attack_castinga").size(), 1u);
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("mag_rh_ff_r_release").size(), 1u);
        EXPECT_EQ(MWRender::SkyrimClipMeta::sidecarAliases("attack_releasespitx").size(), 1u);
    }
}
