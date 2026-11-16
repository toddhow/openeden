#ifndef OPENMW_MWGUI_SESSIONPRESENTATION_H
#define OPENMW_MWGUI_SESSIONPRESENTATION_H

#include <string>
#include <string_view>

// Task 0215: how a game pack's session is presented, as pure decisions (the pack settings `menus` and `music` of
// [extension.<game>], read by the engine where it has to choose: the intro movies, the title music, the theme).
//
// A session is "neutral" when a game pack is loaded and [Game] neutral world is on. Every decision is false for any
// other session, so Morrowind (and a pack session with these settings off) is unchanged.
//
//  - menus on:  Morrowind's intro movies are not played (the pack's own logo is, if its file is there) and Morrowind's
//               title music is not played;
//  - music on:  the pack's main menu theme replaces Morrowind's title music;
//  - either on: Morrowind's music script gives way (see the pack's `skyrimse.presentation` namespace).

namespace MWGui::SessionPresentation
{
    struct Policy
    {
        bool mNeutralSession = false;
        bool mMenus = false;
        bool mMusic = false;

        /// Morrowind's company and game logo movies are not played.
        [[nodiscard]] bool skipsMorrowindIntro() const { return mNeutralSession && mMenus; }
        /// Morrowind's title music is not played at the main menu (the pack's theme or silence).
        [[nodiscard]] bool replacesTitleMusic() const { return mNeutralSession && (mMenus || mMusic); }
        /// The pack plays its main menu theme.
        [[nodiscard]] bool playsPackTheme() const { return mNeutralSession && mMusic; }
        /// The UI theme of a neutral session with the pack's menus.
        [[nodiscard]] bool usesPackTheme() const { return mNeutralSession && mMenus; }
    };

    [[nodiscard]] inline Policy makePolicy(bool neutralWorld, std::string_view gamePackId, bool menus, bool music)
    {
        Policy p;
        p.mNeutralSession = neutralWorld && !gamePackId.empty();
        p.mMenus = p.mNeutralSession && menus;
        p.mMusic = p.mNeutralSession && music;
        return p;
    }

    /// The title music of the main menu: `morrowind` (the engine's own) unless the policy replaces it. A pack theme is
    /// used only when its file exists (`themeExists`), else nothing plays. Empty means no music.
    [[nodiscard]] inline std::string_view titleMusic(
        const Policy& policy, std::string_view morrowind, std::string_view packTheme, bool themeExists)
    {
        if (!policy.replacesTitleMusic())
            return morrowind;
        if (policy.playsPackTheme() && themeExists)
            return packTheme;
        return {};
    }

    /// Which company logo movie to play at start-up: Morrowind's own (`morrowind`) when the intro is not skipped, else
    /// the pack's logo if its file exists, else none.
    [[nodiscard]] inline std::string_view companyLogo(
        const Policy& policy, std::string_view morrowind, std::string_view packLogo, bool logoExists)
    {
        if (!policy.skipsMorrowindIntro())
            return morrowind;
        return logoExists ? packLogo : std::string_view{};
    }

    /// The Morrowind game logo movie, played at the main menu: only Morrowind sessions play it.
    [[nodiscard]] inline std::string_view gameLogo(const Policy& policy, std::string_view morrowind)
    {
        return policy.skipsMorrowindIntro() ? std::string_view{} : morrowind;
    }

    inline constexpr std::string_view kPackThemeFile = "music/special/mus_maintheme.xwm";
    inline constexpr std::string_view kPackLogoMovie = "BGS_Logo.bik";
    inline constexpr std::string_view kPackThemeName = "skyrim";

    /// The theme the overlay uses: the configured one, except that a neutral session with the pack's menus swaps the
    /// default `neutral` for the pack theme (a user who names another theme keeps it; the pack setting
    /// `ui theme = neutral` is how to ask for the Morrowind look in such a session, see packThemeSetting).
    [[nodiscard]] inline std::string chooseTheme(const Policy& policy, std::string_view configured,
        std::string_view packThemeSetting, std::string_view defaultTheme = "neutral")
    {
        if (policy.usesPackTheme() && configured == defaultTheme)
            return std::string(packThemeSetting.empty() ? kPackThemeName : packThemeSetting);
        return std::string(configured);
    }
}

#endif
