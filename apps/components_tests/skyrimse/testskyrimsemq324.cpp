// Task 0324: the pieces the main quest after the push trial needs. (1) FollowerState keeps the relation a script gave
// two factions (Faction.SetAlly and SetEnemy: the Greybeards become the player's allies at MQ105 stage 5), both ways.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/follower_state.hpp>
#include <game_skyrimse/placed_refs.hpp>

#include <gtest/gtest.h>

#include <span>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kPlayerFaction = 0xDB1;
    constexpr FormId kGreybeards = 0x1234;

    TEST(SkyrimMq324Relations, noScriptMeansNoOpinion)
    {
        FollowerState state;
        EXPECT_FALSE(state.factionRelation(kPlayerFaction, kGreybeards).has_value());
    }

    TEST(SkyrimMq324Relations, aRelationAScriptGaveHoldsBothWays)
    {
        FollowerState state;
        EXPECT_TRUE(state.setFactionRelation(kGreybeards, kPlayerFaction, 2));
        ASSERT_TRUE(state.factionRelation(kPlayerFaction, kGreybeards).has_value());
        EXPECT_EQ(*state.factionRelation(kPlayerFaction, kGreybeards), 2);
        EXPECT_EQ(*state.factionRelation(kGreybeards, kPlayerFaction), 2);
    }

    TEST(SkyrimMq324Relations, theLastRelationWinsAndAZeroIdIsRefused)
    {
        FollowerState state;
        EXPECT_FALSE(state.setFactionRelation(0, kPlayerFaction, 2));
        EXPECT_TRUE(state.setFactionRelation(kGreybeards, kPlayerFaction, 2));
        EXPECT_TRUE(state.setFactionRelation(kPlayerFaction, kGreybeards, 1));
        EXPECT_EQ(*state.factionRelation(kGreybeards, kPlayerFaction), 1);
    }

    // (2) GetTalkedToPC outside a conversation: Lydia's greeting package (HousecarlWhiterunGreetPlayer) needs
    // "GetTalkedToPC == 0" and must stop once the player has talked to her; the world asks a lookup by reference.
    struct TalkedFixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        conditions::Evaluator mEval;

        bool talkedIs(float value)
        {
            survey::Ctda c;
            c.mFunction = 50; // GetTalkedToPC
            c.mCompare = 0;
            c.mComparison = value;
            return mEval.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimMq324Talked, withNoLookupAndNoConversationNobodyHasBeenTalkedTo)
    {
        TalkedFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = 0x100;
        speaker.mReference = 0x200;
        f.mWorld.setSpeaker(speaker);
        EXPECT_TRUE(f.talkedIs(0.f));
        EXPECT_FALSE(f.talkedIs(1.f));
    }

    TEST(SkyrimMq324Talked, theLookupMakesAnActorTalkedToAfterItsConversation)
    {
        TalkedFixture f;
        DialogueSpeaker speaker;
        speaker.mBase = 0x100;
        speaker.mReference = 0x200;
        f.mWorld.setSpeaker(speaker);
        f.mWorld.setTalkedLookup([](FormId reference) { return reference == 0x200; });
        EXPECT_TRUE(f.talkedIs(1.f));
        speaker.mReference = 0x201; // another placement of the same base
        f.mWorld.setSpeaker(speaker);
        EXPECT_TRUE(f.talkedIs(0.f));
    }

    // (3) GetRelationshipRank with a target of 0: the player (the recruit lines of a potential follower are written
    // that way). Lydia's RELA names the Player NPC_ (0x7) as its child.
    TEST(SkyrimMq324Relationship, aTargetOfZeroIsThePlayerAndTheRecordWithThePlayerIsFound)
    {
        constexpr FormId kLydia = 0x01000200;
        constexpr FormId kLydiaRef = 0x01000500;
        constexpr FormId kCell = 0x01000100;
        ActorIndex actors;
        ActorRecords ar;
        ActorNpc npc;
        npc.mCommon.mId = kLydia;
        ar.mNpcs.push_back(npc);
        actors.add(ar);
        DialogueIndex dialogue;
        PlacedRefIndex refs;
        PlacedRefRecords records;
        PlacedEntry entry;
        entry.mId = kLydiaRef;
        entry.mBase = kLydia;
        entry.mCell = kCell;
        entry.mFlags = PlacedFlags::IsActor;
        records.mRefs.push_back(entry);
        RelationshipRecord relation;
        relation.mId = 0x01000700;
        relation.mParent = kLydia;
        relation.mChild = 0x00000007;
        relation.mRank = 1; // Ally (HousecarlWhiterunPlayerRelationship)
        records.mRelations.push_back(relation);
        refs.add(std::move(records));
        refs.finish();
        SkyrimConditionWorld world(actors, dialogue);
        world.setPlacedRefs(&refs);
        DialogueSpeaker speaker;
        speaker.mBase = kLydia;
        speaker.mReference = kLydiaRef;
        speaker.mCell = kCell;
        world.setSpeaker(speaker);
        survey::Ctda c;
        c.mFunction = 403; // GetRelationshipRank
        c.mParam1 = 0;
        c.mCompare = 3; // >=
        c.mComparison = 1.f;
        EXPECT_TRUE(world.answers(c));
        conditions::Evaluator eval;
        EXPECT_TRUE(eval.evaluate(std::span<const survey::Ctda>(&c, 1), world));
    }

    TEST(SkyrimMq324Relations, theSavedPartOfTheStateIsUntouched)
    {
        FollowerState state;
        (void)state.setFactionRelation(kGreybeards, kPlayerFaction, 2);
        EXPECT_TRUE(state.data() == FollowerStateData());
    }
}
