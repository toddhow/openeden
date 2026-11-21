// Task 0343 (Skyrim Dark Brotherhood, DB02 to its end): GetDeadCount(actor base) counts the placements of that base the
// session killed (Astrid's second greeting, DBAstridSecondGreetBranch, asks GetDeadCount of each captive's base).
// Written first and run red against the old answer (always 0). Generated records; no game data.
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <set>
#include <span>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kCaptive1 = 0x01000200;
    constexpr FormId kCaptive2 = 0x01000201;
    constexpr FormId kRef1 = 0x01000500;
    constexpr FormId kRef2a = 0x01000501;
    constexpr FormId kRef2b = 0x01000502;
    constexpr FormId kCell = 0x01000100;

    survey::Ctda getDeadCountOf(FormId base, float value)
    {
        survey::Ctda c;
        c.mFunction = 84; // GetDeadCount
        c.mParam1 = base;
        c.mCompare = 0; // ==
        c.mComparison = value;
        return c;
    }

    struct Rig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        std::set<FormId> mKilled;
        Rig()
        {
            ActorRecords ar;
            ActorNpc a;
            a.mCommon.mId = kCaptive1;
            ar.mNpcs.push_back(a);
            ActorNpc b;
            b.mCommon.mId = kCaptive2;
            ar.mNpcs.push_back(b);
            mActors.add(ar);
            PlacedRefRecords r;
            const auto place = [&r](FormId id, FormId base) {
                PlacedEntry e;
                e.mId = id;
                e.mBase = base;
                e.mCell = kCell;
                e.mFlags = PlacedFlags::IsActor;
                r.mRefs.push_back(e);
            };
            place(kRef1, kCaptive1);
            place(kRef2a, kCaptive2);
            place(kRef2b, kCaptive2);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
        }
        bool count(FormId base, float value)
        {
            const survey::Ctda c = getDeadCountOf(base, value);
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimDb03DeadCount, nobodyKilledIsZeroWithOrWithoutALookup)
    {
        Rig rig;
        EXPECT_TRUE(rig.count(kCaptive1, 0.f));
        rig.mWorld.setDeadLookup([&rig](FormId ref) { return rig.mKilled.count(ref) != 0; });
        EXPECT_TRUE(rig.count(kCaptive1, 0.f));
        EXPECT_TRUE(rig.count(kCaptive2, 0.f));
    }

    TEST(SkyrimDb03DeadCount, aKilledPlacementCountsForItsBaseOnly)
    {
        Rig rig;
        rig.mWorld.setDeadLookup([&rig](FormId ref) { return rig.mKilled.count(ref) != 0; });
        rig.mKilled.insert(kRef1);
        EXPECT_TRUE(rig.count(kCaptive1, 1.f));
        EXPECT_TRUE(rig.count(kCaptive2, 0.f));
    }

    TEST(SkyrimDb03DeadCount, everyKilledPlacementOfABaseIsCounted)
    {
        Rig rig;
        rig.mWorld.setDeadLookup([&rig](FormId ref) { return rig.mKilled.count(ref) != 0; });
        rig.mKilled.insert(kRef2a);
        EXPECT_TRUE(rig.count(kCaptive2, 1.f));
        rig.mKilled.insert(kRef2b);
        EXPECT_TRUE(rig.count(kCaptive2, 2.f));
        EXPECT_FALSE(rig.count(kCaptive2, 1.f));
    }
}
