// Task 0318 (the main quest after Dragon Rising): MQ104OutroScene's phases 15, 16 and 18 start only when
// GetDistance(Player alias) < 1000 runs on Balgruuf's alias. The conditions world measured such a distance only from
// the player; an actor-to-alias distance (the subject a placed actor, the alias the player or another placed reference)
// was "unknown", so the phases were skipped. Written first and run red against the old measure.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/placed_refs.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kNpc = 0x01000200;
    constexpr FormId kBalgruuf = 0x01000500; // placed in cell 0x100 at (0, 0, 0)
    constexpr FormId kGuard = 0x01000501; // placed in cell 0x100 at (30, 40, 0)
    constexpr FormId kFarAway = 0x01000502; // placed in cell 0x101
    constexpr FormId kCell = 0x01000100;
    constexpr std::uint32_t kPlayerAlias = 41;
    constexpr std::uint32_t kGuardAlias = 7;
    constexpr std::uint32_t kFarAlias = 8;

    struct Rig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        Rig()
        {
            ActorRecords ar;
            ActorNpc a;
            a.mCommon.mId = kNpc;
            ar.mNpcs.push_back(a);
            mActors.add(ar);
            PlacedRefRecords r;
            const auto place = [&r](FormId id, FormId cell, float x, float y) {
                PlacedEntry e;
                e.mId = id;
                e.mBase = kNpc;
                e.mCell = cell;
                e.mX = x;
                e.mY = y;
                e.mFlags = PlacedFlags::IsActor;
                r.mRefs.push_back(e);
            };
            place(kBalgruuf, kCell, 0.f, 0.f);
            place(kGuard, kCell, 30.f, 40.f);
            place(kFarAway, 0x01000101, 10.f, 10.f);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
            mWorld.setAliasView([](std::uint32_t alias) -> FormId {
                switch (alias)
                {
                    case kPlayerAlias:
                        return 0x14; // PlayerRef
                    case kGuardAlias:
                        return kGuard;
                    case kFarAlias:
                        return kFarAway;
                    default:
                        return kNullFormId;
                }
            });
            mWorld.setPlayerPlace(true, true, kCell, 0);
            mWorld.setPlayerPosition(3.0, 4.0, 0.0);
        }
        static conditions::Subject placed(FormId ref) { return SkyrimConditionWorld::kPlacedTag | ref; }
    };

    TEST(SkyrimAliasDistance, aPlacedActorIsMeasuredToTheAliasThatHoldsThePlayer)
    {
        Rig r;
        EXPECT_FLOAT_EQ(r.mWorld.distanceToAlias(Rig::placed(kBalgruuf), kPlayerAlias), 5.f);
    }

    TEST(SkyrimAliasDistance, aPlacedActorIsMeasuredToAnAliasHoldingAnotherPlacedReferenceOfItsCell)
    {
        Rig r;
        EXPECT_FLOAT_EQ(r.mWorld.distanceToAlias(Rig::placed(kBalgruuf), kGuardAlias), 50.f);
    }

    TEST(SkyrimAliasDistance, anActorIsNotMeasuredToTheAliasOfAnotherCell)
    {
        Rig r;
        EXPECT_GT(r.mWorld.distanceToAlias(Rig::placed(kBalgruuf), kFarAlias), 1e30f);
    }

    TEST(SkyrimAliasDistance, anActorIsNotMeasuredToThePlayerWhoStandsInAnotherCell)
    {
        Rig r;
        r.mWorld.setPlayerPlace(true, true, 0x01000101, 0);
        EXPECT_GT(r.mWorld.distanceToAlias(Rig::placed(kBalgruuf), kPlayerAlias), 1e30f);
    }

    TEST(SkyrimAliasDistance, anAliasThatHoldsNothingIsUnknown)
    {
        Rig r;
        EXPECT_GT(r.mWorld.distanceToAlias(Rig::placed(kBalgruuf), 99), 1e30f);
    }

    TEST(SkyrimAliasDistance, thePlayerIsStillMeasuredToAnAliasHoldingAPlacedReference)
    {
        Rig r;
        EXPECT_FLOAT_EQ(r.mWorld.distanceToAlias(SkyrimConditionWorld::kPlayer, kGuardAlias), 45.f);
    }
}
