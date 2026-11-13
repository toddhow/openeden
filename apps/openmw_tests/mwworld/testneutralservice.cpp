// Task 0155: what the core says about a response that is a service the pack cannot run, and how such a turn looks in
// the conversation box (it ends the conversation: one Goodbye button, no topics).
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutraldialogue.hpp"

namespace
{
    using MWWorld::NeutralDialogueChoice;
    using MWWorld::NeutralDialoguePager;
    using openeden::sdk::game::DialogueTurn;

    TEST(NeutralServiceNote, anOrdinaryTurnHasNoNote)
    {
        DialogueTurn turn;
        turn.text = "Hello.";
        EXPECT_TRUE(MWWorld::neutralServiceNote(turn, "Hulda").empty());
    }

    TEST(NeutralServiceNote, aServiceTurnNamesTheServiceAndTheSpeaker)
    {
        DialogueTurn turn;
        turn.service = "barter";
        EXPECT_EQ(MWWorld::neutralServiceNote(turn, "Hulda"), "service not implemented: barter with Hulda");
    }

    TEST(NeutralServiceNote, theTurnsOwnSpeakerNameWinsAndAMissingOneIsSaid)
    {
        DialogueTurn turn;
        turn.service = "training";
        turn.speakerName = "Carl the Smith";
        EXPECT_EQ(MWWorld::neutralServiceNote(turn, "Hulda"), "service not implemented: training with Carl the Smith");
        turn.speakerName.clear();
        EXPECT_EQ(MWWorld::neutralServiceNote(turn, ""), "service not implemented: training with the speaker");
    }

    TEST(NeutralServiceNote, aServiceTurnClosesTheBoxCleanly)
    {
        DialogueTurn turn;
        turn.text = "Let us see.";
        turn.service = "barter";
        turn.endsConversation = true;
        turn.topics.push_back({ 7, "left over" }); // a pack must not send topics with an ended turn; the box drops them
        NeutralDialoguePager pager("Goodbye", "More", 8);
        pager.setTurn(turn, "Hulda");
        EXPECT_TRUE(pager.ended());
        const MWWorld::NeutralDialogueMenu menu = pager.menu();
        ASSERT_EQ(menu.buttons.size(), 1u);
        EXPECT_EQ(menu.buttons[0], "Goodbye");
        EXPECT_EQ(pager.press(0).kind, NeutralDialogueChoice::Kind::Goodbye);
    }
}
