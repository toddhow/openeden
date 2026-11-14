// Task 0141 (milestone M-S4 slice 1): what a neutral conversation's message box holds and what a button means. Pure:
// no window manager, no pack. (Written after the pager, not red first; mutations are listed in the task log.)
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutraldialogue.hpp"

namespace
{
    using MWWorld::NeutralDialogueChoice;
    using MWWorld::NeutralDialoguePager;
    using openeden::sdk::game::DialogueTurn;

    DialogueTurn turnWithTopics(std::size_t n, bool ends = false)
    {
        DialogueTurn turn;
        turn.text = "Well met.";
        turn.endsConversation = ends;
        for (std::size_t i = 0; i < n; ++i)
            turn.topics.push_back({ static_cast<std::uint32_t>(100 + i), "Topic " + std::to_string(i) });
        return turn;
    }

    TEST(NeutralDialoguePager, theMessageIsTheSpeakersNameAndTheLine)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(2), "Hulda");
        const auto menu = pager.menu();
        EXPECT_EQ(menu.message, "Hulda\n\nWell met.");
        EXPECT_EQ(menu.buttons, (std::vector<std::string>{ "Topic 0", "Topic 1", "Goodbye" }));
    }

    TEST(NeutralDialoguePager, theTurnsOwnSpeakerNameWins)
    {
        NeutralDialoguePager pager("Goodbye", "More");
        DialogueTurn turn = turnWithTopics(0);
        turn.speakerName = "Hulda the Innkeeper";
        pager.setTurn(turn, "Hulda");
        EXPECT_EQ(pager.menu().message, "Hulda the Innkeeper\n\nWell met.");
        pager.setTurn(turnWithTopics(0), "");
        EXPECT_EQ(pager.menu().message, "Well met.");
    }

    TEST(NeutralDialoguePager, aTopicButtonGivesItsId)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(2), "H");
        const auto choice = pager.press(1);
        EXPECT_EQ(choice.kind, NeutralDialogueChoice::Kind::Topic);
        EXPECT_EQ(choice.topicId, 101u);
    }

    TEST(NeutralDialoguePager, goodbyeIsTheLastButton)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(2), "H");
        EXPECT_EQ(pager.press(2).kind, NeutralDialogueChoice::Kind::Goodbye);
        EXPECT_EQ(pager.press(3).kind, NeutralDialogueChoice::Kind::None);
        EXPECT_EQ(pager.press(-1).kind, NeutralDialogueChoice::Kind::None);
    }

    TEST(NeutralDialoguePager, aLongListIsPagedAndMoreWraps)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(7), "H");
        EXPECT_EQ(pager.pageCount(), 3u);
        EXPECT_EQ(
            pager.menu().buttons, (std::vector<std::string>{ "Topic 0", "Topic 1", "Topic 2", "More", "Goodbye" }));
        EXPECT_EQ(pager.press(3).kind, NeutralDialogueChoice::Kind::MoreTopics);
        EXPECT_EQ(
            pager.menu().buttons, (std::vector<std::string>{ "Topic 3", "Topic 4", "Topic 5", "More", "Goodbye" }));
        EXPECT_EQ(pager.press(3).kind, NeutralDialogueChoice::Kind::MoreTopics);
        EXPECT_EQ(pager.menu().buttons, (std::vector<std::string>{ "Topic 6", "More", "Goodbye" }));
        const auto last = pager.press(0);
        EXPECT_EQ(last.kind, NeutralDialogueChoice::Kind::Topic);
        EXPECT_EQ(last.topicId, 106u);
        EXPECT_EQ(pager.press(1).kind, NeutralDialogueChoice::Kind::MoreTopics);
        EXPECT_EQ(pager.menu().buttons.front(), "Topic 0"); // wrapped
        EXPECT_EQ(pager.press(4).kind, NeutralDialogueChoice::Kind::Goodbye);
    }

    TEST(NeutralDialoguePager, aNewTurnStartsOnTheFirstPage)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(7), "H");
        (void)pager.press(3);
        pager.setTurn(turnWithTopics(7), "H");
        EXPECT_EQ(pager.menu().buttons.front(), "Topic 0");
    }

    TEST(NeutralDialoguePager, aTurnThatEndsOffersOnlyGoodbye)
    {
        NeutralDialoguePager pager("Goodbye", "More", 3);
        pager.setTurn(turnWithTopics(5, true), "H");
        EXPECT_TRUE(pager.ended());
        EXPECT_EQ(pager.menu().buttons, (std::vector<std::string>{ "Goodbye" }));
        EXPECT_EQ(pager.press(0).kind, NeutralDialogueChoice::Kind::Goodbye);
    }

    TEST(NeutralDialoguePressScript, entriesAreSortedAndBadOnesSkipped)
    {
        using Script = std::vector<std::pair<int, int>>;
        EXPECT_EQ(MWWorld::parseNeutralDialoguePressScript("10:2,3:0"), (Script{ { 3, 0 }, { 10, 2 } }));
        EXPECT_EQ(MWWorld::parseNeutralDialoguePressScript("x:1,2:,:3,4:5,6"), (Script{ { 4, 5 } }));
        EXPECT_TRUE(MWWorld::parseNeutralDialoguePressScript("").empty());
        EXPECT_TRUE(MWWorld::parseNeutralDialoguePressScript("99999999:1,1:99999").empty()); // too long
    }

    TEST(NeutralDialoguePager, noTopicsStillOffersGoodbye)
    {
        NeutralDialoguePager pager("Goodbye", "More");
        pager.setTurn(turnWithTopics(0), "H");
        EXPECT_EQ(pager.menu().buttons, (std::vector<std::string>{ "Goodbye" }));
        EXPECT_EQ(pager.pageCount(), 1u);
    }
}
