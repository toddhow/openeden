// Task 0172 (combat stage S5-4): the player's attack key as the pack sees it. NeutralPlayerAttackInput
// (apps/openmw/mwphysics/neutralplayerattack.hpp, pure) turns frames of "the action is down" into one attack or power
// attack, and NeutralCombatBridge::playerAttack hands it to the pack. Written from the header before the code.
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw/mwphysics/neutralplayerattack.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    using MWPhysics::NeutralPlayerAttackInput;
    using Event = NeutralPlayerAttackInput::Event;

    constexpr float kFrame = 1.f / 60.f;

    TEST(NeutralPlayerAttackInput, nothingHappensWhileTheKeyIsUp)
    {
        NeutralPlayerAttackInput input;
        for (int i = 0; i < 100; ++i)
            EXPECT_EQ(input.update(false, true, kFrame), Event::None);
        EXPECT_FALSE(input.holding());
    }

    TEST(NeutralPlayerAttackInput, aTapIsAnAttackToldOnTheRelease)
    {
        NeutralPlayerAttackInput input;
        EXPECT_EQ(input.update(true, true, kFrame), Event::None);
        EXPECT_TRUE(input.holding());
        for (int i = 0; i < 5; ++i)
            EXPECT_EQ(input.update(true, true, kFrame), Event::None);
        EXPECT_EQ(input.update(false, true, kFrame), Event::Attack);
        EXPECT_FALSE(input.holding());
        EXPECT_EQ(input.update(false, true, kFrame), Event::None); // told once
    }

    TEST(NeutralPlayerAttackInput, aHoldBecomesAPowerAttackAtTheThresholdAndTheReleaseTellsNothing)
    {
        NeutralPlayerAttackInput input;
        float held = 0.f;
        Event fired = Event::None;
        int firedAt = -1;
        for (int i = 0; i < 120 && fired == Event::None; ++i)
        {
            fired = input.update(true, true, kFrame);
            held += kFrame;
            if (fired != Event::None)
                firedAt = i;
        }
        EXPECT_EQ(fired, Event::PowerAttack);
        // The first frame down adds no time (it is the press), so the 0.5 s are reached on frame 31 (index 30).
        EXPECT_GE(held, NeutralPlayerAttackInput::kPowerHoldSeconds);
        EXPECT_NEAR(firedAt * kFrame, NeutralPlayerAttackInput::kPowerHoldSeconds, 2 * kFrame);
        // Still held: nothing more; the release: nothing either.
        for (int i = 0; i < 30; ++i)
            EXPECT_EQ(input.update(true, true, kFrame), Event::None);
        EXPECT_EQ(input.update(false, true, kFrame), Event::None);
    }

    TEST(NeutralPlayerAttackInput, aTapJustUnderTheThresholdIsStillAnAttack)
    {
        NeutralPlayerAttackInput input;
        EXPECT_EQ(input.update(true, true, 0.3f), Event::None);
        EXPECT_EQ(input.update(true, true, 0.19f), Event::None); // 0.19 in all after the press frame
        EXPECT_EQ(input.update(false, true, kFrame), Event::Attack);
    }

    TEST(NeutralPlayerAttackInput, aSecondPressAfterARelease)
    {
        NeutralPlayerAttackInput input;
        (void)input.update(true, true, kFrame);
        EXPECT_EQ(input.update(false, true, kFrame), Event::Attack);
        (void)input.update(true, true, kFrame);
        EXPECT_EQ(input.update(false, true, kFrame), Event::Attack);
    }

    TEST(NeutralPlayerAttackInput, disabledControlsTellNothingAndForgetAHold)
    {
        NeutralPlayerAttackInput input;
        (void)input.update(true, true, kFrame);
        // A menu opens while the key is down: no event, and the release in the menu is not an attack.
        EXPECT_EQ(input.update(true, false, kFrame), Event::None);
        EXPECT_FALSE(input.holding());
        EXPECT_EQ(input.update(false, false, kFrame), Event::None);
        EXPECT_EQ(input.update(false, true, kFrame), Event::None);
        // A key already down when the menu closes does not count as a new press until it is released and pressed.
        EXPECT_EQ(input.update(true, false, kFrame), Event::None);
        EXPECT_EQ(input.update(true, true, kFrame), Event::None);
        EXPECT_EQ(input.update(false, true, kFrame), Event::None);
    }

    TEST(NeutralPlayerAttackInput, aHugeOrBadFrameTimeNeverFiresEarlyOrTwice)
    {
        NeutralPlayerAttackInput input;
        EXPECT_EQ(input.update(true, true, std::numeric_limits<float>::quiet_NaN()), Event::None);
        EXPECT_EQ(input.update(true, true, -3.f), Event::None);
        EXPECT_EQ(input.update(false, true, kFrame), Event::Attack);
        (void)input.update(true, true, kFrame);
        EXPECT_EQ(input.update(true, true, 10.f), Event::PowerAttack);
        EXPECT_EQ(input.update(true, true, 10.f), Event::None);
    }

    // ---- the bridge ----------------------------------------------------------------------------------------------

    class RecordingRules final : public openeden::sdk::game::CombatRules
    {
    public:
        void onClipEvent(const openeden::sdk::game::ClipEvent&) override {}
        void onSweepResult(openeden::sdk::game::CombatSubject, std::uint32_t, const openeden::sdk::game::SweepHit*,
            std::size_t) override
        {
        }
        void combatUpdate(float, openeden::sdk::game::CombatRequestWriter&) override {}
        void onPlayerAttack(bool power) override
        {
            if (mThrow)
                throw std::runtime_error("pack failure");
            ++mCalls;
            mLast = power;
        }
        int mCalls = 0;
        bool mLast = false;
        bool mThrow = false;
    };

    TEST(NeutralCombatBridgePlayerAttack, tellsThePackOnceWithTheKindOfAttack)
    {
        MWPhysics::NeutralActorBridge actors;
        MWPhysics::NeutralCombatBridge bridge(actors);
        RecordingRules rules;
        EXPECT_TRUE(bridge.playerAttack(&rules, false));
        EXPECT_EQ(rules.mCalls, 1);
        EXPECT_FALSE(rules.mLast);
        EXPECT_TRUE(bridge.playerAttack(&rules, true));
        EXPECT_EQ(rules.mCalls, 2);
        EXPECT_TRUE(rules.mLast);
        EXPECT_EQ(bridge.stats().mPlayerAttacks, 2u);
        EXPECT_EQ(bridge.stats().mPlayerAttackRefused, 0u);
    }

    TEST(NeutralCombatBridgePlayerAttack, noRulesIsRefusedAndCounted)
    {
        MWPhysics::NeutralActorBridge actors;
        MWPhysics::NeutralCombatBridge bridge(actors);
        EXPECT_FALSE(bridge.playerAttack(nullptr, false));
        EXPECT_EQ(bridge.stats().mPlayerAttacks, 0u);
        EXPECT_EQ(bridge.stats().mPlayerAttackRefused, 1u);
    }

    TEST(NeutralCombatBridgePlayerAttack, aPackThatThrowsIsCaughtAndCounted)
    {
        MWPhysics::NeutralActorBridge actors;
        MWPhysics::NeutralCombatBridge bridge(actors);
        RecordingRules rules;
        rules.mThrow = true;
        EXPECT_NO_THROW(EXPECT_FALSE(bridge.playerAttack(&rules, true)));
        EXPECT_EQ(bridge.stats().mPlayerAttackRefused, 1u);
        EXPECT_EQ(bridge.stats().mPlayerAttacks, 0u);
    }
}
