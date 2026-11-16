// Task 0184 (loot, equip and keys): when a dead actor's activation opens the loot window, how a key is held, and how
// the
// engine finds out that a neutral actor is dead. Written before the code and run red against stubs first.
#include <gtest/gtest.h>

#include <functional>

#include "apps/openmw/mwphysics/neutralactorbridge.hpp"
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"
#include "apps/openmw/mwworld/neutralinventory.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using namespace MWWorld;

    TEST(NeutralLootDecision, onlyThePlayerActivatingADeadActorWithTheSettingOnOpensTheLootWindow)
    {
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Actor, true, true, true), NeutralInventoryDecision::Open);
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Actor, true, true, false), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Actor, true, false, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Actor, false, true, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Container, true, true, true), NeutralInventoryDecision::NotApplicable);
        EXPECT_EQ(decideNeutralLoot(ObjectKind::Door, true, true, true), NeutralInventoryDecision::NotApplicable);
    }

    TEST(NeutralLootEvent, theKindOfACorpseIsActorAndTheNameRides)
    {
        const auto fields = neutralInventoryEventFields(0x000A1B2C, 0x00013BBF, "actor", "Hulda");
        ASSERT_EQ(fields.size(), 4u);
        EXPECT_EQ(fields[2].mName, "kind");
        EXPECT_EQ(fields[2].mText, "actor");
        EXPECT_EQ(fields[3].mText, "Hulda");
    }

    TEST(NeutralKeyHeld, theRingOrTheInventoryHoldsIt)
    {
        const auto ring = [](WorldId key) { return key == 0x10; };
        const auto bag = [](WorldId key) { return key == 0x20; };
        EXPECT_TRUE(neutralKeyHeld(0x10, ring, bag));
        EXPECT_TRUE(neutralKeyHeld(0x20, ring, bag));
        EXPECT_FALSE(neutralKeyHeld(0x30, ring, bag));
    }

    TEST(NeutralKeyHeld, withoutAnInventoryTheRingAloneDecidesAsBefore)
    {
        const auto ring = [](WorldId key) { return key == 0x10; };
        EXPECT_TRUE(neutralKeyHeld(0x10, ring, {}));
        EXPECT_FALSE(neutralKeyHeld(0x20, ring, {}));
        EXPECT_FALSE(neutralKeyHeld(0x10, {}, {}));
    }

    TEST(NeutralKeyHeld, noKeyIsNeverHeldAndTheInventoryIsOnlyAskedWhenTheRingSaysNo)
    {
        int asked = 0;
        const auto ring = [](WorldId key) { return key == 0x10; };
        const auto bag = [&](WorldId) {
            ++asked;
            return true;
        };
        EXPECT_FALSE(neutralKeyHeld(0, ring, bag));
        EXPECT_EQ(asked, 0);
        EXPECT_TRUE(neutralKeyHeld(0x10, ring, bag));
        EXPECT_EQ(asked, 0);
        EXPECT_TRUE(neutralKeyHeld(0x99, ring, bag));
        EXPECT_EQ(asked, 1);
    }

    class Rules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            if (mDo)
                mDo(writer);
            mDo = nullptr;
        }
        std::function<void(CombatRequestWriter&)> mDo;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void setHeading(const void*, float) override {}
    };

    struct DeadByKey : ::testing::Test
    {
        MWPhysics::NeutralActorBridge mActors;
        MWPhysics::NeutralCombatBridge mBridge{ mActors };
        Rules mRules;
        Sink mSink;
        MWPhysics::NeutralClipMetaFn mMeta = [](const void*) { return HavokPack::ClipProgress(); };
        int mKeyA = 0;
        int mKeyB = 0;
        ActorHandle mHandleA = mActors.add(nullptr, &mKeyA, 11, 1, "A", 0, 0, 0, 0);
        ActorHandle mHandleB = mActors.add(nullptr, &mKeyB, 12, 1, "B", 0, 0, 0, 0);

        void frame(std::function<void(CombatRequestWriter&)> doIt = nullptr)
        {
            mRules.mDo = std::move(doIt);
            mBridge.update(&mRules, 0.1f, mSink, mMeta, nullptr);
        }
    };

    TEST_F(DeadByKey, anActorTheEngineHasNotBeenToldDeadIsAlive)
    {
        EXPECT_FALSE(mBridge.isDeadByKey(&mKeyA));
        EXPECT_FALSE(mBridge.isDeadByKey(&mKeyB));
    }

    TEST_F(DeadByKey, theKeyOfTheActorTheRulesReportedDeadIsDeadAndOnlyThatOne)
    {
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDeath(mHandleA)); });
        EXPECT_TRUE(mBridge.isDeadByKey(&mKeyA));
        EXPECT_FALSE(mBridge.isDeadByKey(&mKeyB));
    }

    TEST_F(DeadByKey, anUnknownKeyIsNeverDeadAndARemovedActorIsForgotten)
    {
        int stranger = 0;
        EXPECT_FALSE(mBridge.isDeadByKey(&stranger));
        EXPECT_FALSE(mBridge.isDeadByKey(nullptr));
        frame([&](CombatRequestWriter& w) { EXPECT_TRUE(w.reportDeath(mHandleA)); });
        mActors.remove(nullptr, &mKeyA);
        EXPECT_FALSE(mBridge.isDeadByKey(&mKeyA));
    }
}
