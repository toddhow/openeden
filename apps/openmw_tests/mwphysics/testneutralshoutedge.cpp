// Task 0205 (contract 0.6): the bridge hands the player's shout control (press and release) to the pack, like the
// attack
// edges of task 0178. A fake pack, no Jolt, no Ptr.
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    class ShoutRules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter&) override {}
        void onPlayerShoutEdge(bool pressed) override
        {
            if (mThrow)
                throw std::runtime_error("the pack failed");
            mEdges.push_back(pressed);
        }
        void onPlayerAttackEdge(bool pressed) override { mAttackEdges.push_back(pressed); }
        std::vector<bool> mEdges;
        std::vector<bool> mAttackEdges;
        bool mThrow = false;
    };

    TEST(NeutralShoutEdge, thePressAndReleaseReachThePackAndNotTheAttackHook)
    {
        NeutralActorBridge actors;
        NeutralCombatBridge combat{ actors };
        ShoutRules rules;
        EXPECT_TRUE(combat.playerShoutEdge(&rules, true));
        EXPECT_TRUE(combat.playerShoutEdge(&rules, false));
        EXPECT_EQ(rules.mEdges, (std::vector<bool>{ true, false }));
        EXPECT_TRUE(rules.mAttackEdges.empty());
        EXPECT_EQ(combat.stats().mPlayerShoutEdges, 2u);
    }

    TEST(NeutralShoutEdge, noRulesIsRefusedAndCounted)
    {
        NeutralActorBridge actors;
        NeutralCombatBridge combat{ actors };
        EXPECT_FALSE(combat.playerShoutEdge(nullptr, true));
        EXPECT_EQ(combat.stats().mPlayerShoutRefused, 1u);
        EXPECT_EQ(combat.stats().mPlayerShoutEdges, 0u);
    }

    TEST(NeutralShoutEdge, aPackThatThrowsIsCaughtAndCounted)
    {
        NeutralActorBridge actors;
        NeutralCombatBridge combat{ actors };
        ShoutRules rules;
        rules.mThrow = true;
        EXPECT_FALSE(combat.playerShoutEdge(&rules, true));
        EXPECT_EQ(combat.stats().mPlayerShoutRefused, 1u);
        EXPECT_EQ(combat.stats().mRulesFailures, 1u);
    }
}
