// Task 0207: the switches a Lua menu script sets for the engine's own menu and loading screen
// (mwgui/menureplacement.hpp).
#include "apps/openmw/mwgui/menureplacement.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{
    namespace MR = MWGui::MenuReplacement;

    struct MenuReplacementTest : testing::Test
    {
        void SetUp() override { MR::reset(); }
        void TearDown() override { MR::reset(); }
    };

    TEST_F(MenuReplacementTest, theEnginesMenuIsNotReplacedByDefault)
    {
        EXPECT_FALSE(MR::mainMenuReplaced());
        EXPECT_TRUE(MR::mainMenuVisibleFor(true, MR::mainMenuReplaced()));
    }

    TEST_F(MenuReplacementTest, aReplacedMenuIsNeverVisible)
    {
        MR::setMainMenuReplaced(true);
        EXPECT_TRUE(MR::mainMenuReplaced());
        EXPECT_FALSE(MR::mainMenuVisibleFor(true, MR::mainMenuReplaced()));
        EXPECT_FALSE(MR::mainMenuVisibleFor(false, MR::mainMenuReplaced()));
        MR::setMainMenuReplaced(false);
        EXPECT_TRUE(MR::mainMenuVisibleFor(true, MR::mainMenuReplaced()));
        EXPECT_FALSE(MR::mainMenuVisibleFor(false, MR::mainMenuReplaced()));
    }

    TEST_F(MenuReplacementTest, theListenerIsToldOnlyOfARealChange)
    {
        int calls = 0;
        MR::setChangeListener([&calls] { ++calls; });
        MR::setMainMenuReplaced(false); // already false
        EXPECT_EQ(calls, 0);
        MR::setMainMenuReplaced(true);
        EXPECT_EQ(calls, 1);
        MR::setMainMenuReplaced(true);
        EXPECT_EQ(calls, 1);
        MR::setMainMenuReplaced(false);
        EXPECT_EQ(calls, 2);
    }

    TEST_F(MenuReplacementTest, aListenerThatWasRemovedIsNotCalled)
    {
        int calls = 0;
        MR::setChangeListener([&calls] { ++calls; });
        MR::setChangeListener({});
        MR::setMainMenuReplaced(true);
        EXPECT_EQ(calls, 0);
    }

    TEST_F(MenuReplacementTest, theLoadingTipIsKeptAndReplaced)
    {
        EXPECT_TRUE(MR::loadingTip().empty());
        MR::setLoadingTip("Dragons are old.");
        EXPECT_EQ(MR::loadingTip(), "Dragons are old.");
        MR::setLoadingTip("Mind the bears.");
        EXPECT_EQ(MR::loadingTip(), "Mind the bears.");
    }

    TEST_F(MenuReplacementTest, aLongTipIsCutAtACharacterBoundary)
    {
        std::string text(MR::kMaxTipBytes - 1, 'a');
        text += "\xC3\xA9"; // two bytes across the limit
        text += "tail";
        MR::setLoadingTip(text);
        EXPECT_EQ(MR::loadingTip().size(), MR::kMaxTipBytes - 1);
        MR::setLoadingTip(std::string(MR::kMaxTipBytes + 50, 'b'));
        EXPECT_EQ(MR::loadingTip().size(), MR::kMaxTipBytes);
    }

    TEST_F(MenuReplacementTest, controlCharactersOtherThanNewlineAreDropped)
    {
        MR::setLoadingTip(std::string("a\x01" "b\nc\x1f" "d"));
        EXPECT_EQ(MR::loadingTip(), "ab\ncd");
    }

    TEST_F(MenuReplacementTest, aRawLoadingMessageKeyIsReplacedWhileATipShows)
    {
        // a neutral session has no Morrowind strings: the label is the bare key of the message
        EXPECT_EQ(MR::loadingLabel("LoadingMessage14", true), "Loading...");
        EXPECT_EQ(MR::loadingLabel("#{sLoadingMessage14}", true), "Loading...");
        // what the engine really asks for while a game loads: its localised key, which a neutral session shows raw
        EXPECT_EQ(MR::loadingLabel("#{OMWEngine:LoadingInProgress}", true), "Loading...");
        EXPECT_EQ(MR::loadingLabel("#{OMWEngine:LoadingInProgress}", false), "#{OMWEngine:LoadingInProgress}");
        EXPECT_EQ(MR::loadingLabel("LoadingMessage14", false), "LoadingMessage14");
        EXPECT_EQ(MR::loadingLabel("#{sLoadingMessage14}", false), "#{sLoadingMessage14}");
        EXPECT_EQ(MR::loadingLabel("Loading cell Whiterun", true), "Loading cell Whiterun");
        EXPECT_EQ(MR::loadingLabel("", true), "");
    }

    TEST_F(MenuReplacementTest, resetForgetsEverything)
    {
        MR::setMainMenuReplaced(true);
        MR::setLoadingTip("x");
        MR::reset();
        EXPECT_FALSE(MR::mainMenuReplaced());
        EXPECT_TRUE(MR::loadingTip().empty());
    }
}
