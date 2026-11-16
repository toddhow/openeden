// Task 0257: the core's side of a game pack's image space: the conversion of the pack's state to the renderer's
// modifier (MWWorld::neutralModifierFrom), and what the renderer does with it (Render::ImageSpace::combine, pinned in
// components-tests). Written after the driver; a mutation check stands in for a red run (see the task log).
#include <gtest/gtest.h>

#include <apps/openmw/mwworld/neutralimagespace.hpp>

namespace
{
    using openeden::sdk::game::PlayerImageSpaceSource;
    using openeden::sdk::game::PlayerImageSpaceState;

    class FakeSource final : public PlayerImageSpaceSource
    {
    public:
        bool mHas = true;
        PlayerImageSpaceState mState;
        bool playerImageSpace(PlayerImageSpaceState& out) override
        {
            if (!mHas)
                return false;
            out = mState;
            return true;
        }
    };

    TEST(NeutralImageSpaceDriver, aSourceWithNothingGivesTheInactiveModifier)
    {
        FakeSource source;
        source.mHas = false;
        source.mState.active = true;
        const Render::ImageSpace::Modifier m = MWWorld::neutralModifierFrom(source);
        EXPECT_FALSE(m.active);
    }

    TEST(NeutralImageSpaceDriver, aStateThatIsNotActiveGivesTheInactiveModifierWhateverItsNumbers)
    {
        FakeSource source;
        source.mState.active = false;
        source.mState.saturationMult = 0.1f;
        source.mState.brightnessAdd = 9.f;
        const Render::ImageSpace::Modifier m = MWWorld::neutralModifierFrom(source);
        EXPECT_FALSE(m.active);
        EXPECT_FLOAT_EQ(m.saturationMult, 1.f);
        EXPECT_FLOAT_EQ(m.brightnessAdd, 0.f);
    }

    TEST(NeutralImageSpaceDriver, anActiveStateIsCopiedNumberForNumber)
    {
        FakeSource source;
        PlayerImageSpaceState& s = source.mState;
        s.active = true;
        s.saturationMult = 0.5f;
        s.saturationAdd = 0.1f;
        s.brightnessMult = 1.5f;
        s.brightnessAdd = 1.1f;
        s.contrastMult = 0.9f;
        s.contrastAdd = 0.2f;
        s.tintAmount = 0.502f;
        s.tint = { 0.592f, 0.773f, 0.875f };
        const Render::ImageSpace::Modifier m = MWWorld::neutralModifierFrom(source);
        EXPECT_TRUE(m.active);
        EXPECT_FLOAT_EQ(m.saturationMult, 0.5f);
        EXPECT_FLOAT_EQ(m.saturationAdd, 0.1f);
        EXPECT_FLOAT_EQ(m.brightnessMult, 1.5f);
        EXPECT_FLOAT_EQ(m.brightnessAdd, 1.1f);
        EXPECT_FLOAT_EQ(m.contrastMult, 0.9f);
        EXPECT_FLOAT_EQ(m.contrastAdd, 0.2f);
        EXPECT_FLOAT_EQ(m.tintAmount, 0.502f);
        EXPECT_FLOAT_EQ(m.tint[0], 0.592f);
        EXPECT_FLOAT_EQ(m.tint[1], 0.773f);
        EXPECT_FLOAT_EQ(m.tint[2], 0.875f);
    }

    TEST(NeutralImageSpaceDriver, aModifierFromTheSourceMeetsTheWeatherThroughCombine)
    {
        FakeSource source;
        source.mState.active = true;
        source.mState.saturationMult = 0.5f;
        const Render::ImageSpace::Modifier m = MWWorld::neutralModifierFrom(source);
        Render::ImageSpace::Effective weather;
        weather.on = true;
        weather.grade.saturation = 0.8f;
        EXPECT_NEAR(Render::ImageSpace::combine(weather, m).grade.saturation, 0.4f, 1e-6f);
    }
}
