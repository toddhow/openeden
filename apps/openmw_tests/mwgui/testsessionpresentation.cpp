// Task 0215: how a game pack's session is presented (intro movies, title music, theme), as pure decisions.
#include <gtest/gtest.h>

#include "apps/openmw/mwgui/sessionpresentation.hpp"

namespace
{
    using namespace MWGui::SessionPresentation;

    TEST(SessionPresentation, aSessionWithNoGamePackOrNoNeutralWorldIsNeverNeutral)
    {
        EXPECT_FALSE(makePolicy(false, "skyrimse", true, true).mNeutralSession);
        EXPECT_FALSE(makePolicy(true, "", true, true).mNeutralSession);
        const Policy p = makePolicy(false, "skyrimse", true, true);
        EXPECT_FALSE(p.mMenus);
        EXPECT_FALSE(p.mMusic);
    }

    TEST(SessionPresentation, aMorrowindSessionChangesNothing)
    {
        const Policy p = makePolicy(false, "", false, false);
        EXPECT_FALSE(p.skipsMorrowindIntro());
        EXPECT_FALSE(p.replacesTitleMusic());
        EXPECT_FALSE(p.playsPackTheme());
        EXPECT_FALSE(p.usesPackTheme());
        EXPECT_EQ(titleMusic(p, "morrowind.mp3", kPackThemeFile, true), "morrowind.mp3");
        EXPECT_EQ(companyLogo(p, "bethesda logo.bik", kPackLogoMovie, true), "bethesda logo.bik");
        EXPECT_EQ(gameLogo(p, "mw_logo.bik"), "mw_logo.bik");
        EXPECT_EQ(chooseTheme(p, "neutral", ""), "neutral");
    }

    TEST(SessionPresentation, aNeutralSessionWithBothSettingsOffChangesNothing)
    {
        const Policy p = makePolicy(true, "skyrimse", false, false);
        EXPECT_TRUE(p.mNeutralSession);
        EXPECT_FALSE(p.skipsMorrowindIntro());
        EXPECT_FALSE(p.replacesTitleMusic());
        EXPECT_EQ(chooseTheme(p, "neutral", ""), "neutral");
    }

    TEST(SessionPresentation, menusSkipTheIntroAndMusicOrMenusReplaceTheTitleMusic)
    {
        const Policy menus = makePolicy(true, "skyrimse", true, false);
        EXPECT_TRUE(menus.skipsMorrowindIntro());
        EXPECT_TRUE(menus.replacesTitleMusic());
        EXPECT_FALSE(menus.playsPackTheme());
        const Policy music = makePolicy(true, "skyrimse", false, true);
        EXPECT_FALSE(music.skipsMorrowindIntro());
        EXPECT_TRUE(music.replacesTitleMusic());
        EXPECT_TRUE(music.playsPackTheme());
    }

    TEST(SessionPresentation, theTitleMusicIsThePackThemeOnlyWithMusicAndTheFile)
    {
        const Policy both = makePolicy(true, "skyrimse", true, true);
        EXPECT_EQ(titleMusic(both, "morrowind.mp3", kPackThemeFile, true), kPackThemeFile);
        EXPECT_TRUE(titleMusic(both, "morrowind.mp3", kPackThemeFile, false).empty());
        const Policy menusOnly = makePolicy(true, "skyrimse", true, false);
        EXPECT_TRUE(titleMusic(menusOnly, "morrowind.mp3", kPackThemeFile, true).empty());
    }

    TEST(SessionPresentation, theIntroIsThePackLogoOrNothingNeverMorrowinds)
    {
        const Policy p = makePolicy(true, "skyrimse", true, false);
        EXPECT_EQ(companyLogo(p, "bethesda logo.bik", kPackLogoMovie, true), kPackLogoMovie);
        EXPECT_TRUE(companyLogo(p, "bethesda logo.bik", kPackLogoMovie, false).empty());
        EXPECT_TRUE(gameLogo(p, "mw_logo.bik").empty());
    }

    TEST(SessionPresentation, thePackThemeReplacesOnlyTheDefaultThemeAndOnlyWithMenus)
    {
        const Policy p = makePolicy(true, "skyrimse", true, false);
        EXPECT_EQ(chooseTheme(p, "neutral", ""), "skyrim");
        EXPECT_EQ(chooseTheme(p, "high-contrast", ""), "high-contrast");
        EXPECT_EQ(chooseTheme(p, "neutral", "neutral"), "neutral");
        EXPECT_EQ(chooseTheme(p, "neutral", "other"), "other");
        EXPECT_EQ(chooseTheme(makePolicy(true, "skyrimse", false, true), "neutral", ""), "neutral");
    }
}
