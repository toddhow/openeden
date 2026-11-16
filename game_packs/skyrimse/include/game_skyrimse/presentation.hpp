#ifndef OPENEDEN_GAME_SKYRIMSE_PRESENTATION_H
#define OPENEDEN_GAME_SKYRIMSE_PRESENTATION_H

// What the pack tells the scripts about how a Skyrim SE session is presented (task 0215). Off unless
// [extension.skyrimse] menus or [extension.skyrimse] music is on: with both off the namespace is not declared at all.
//
// The namespace `core.skyrimse.presentation` (menuSafe) has three read-only functions:
//   menus()        the pack's menus setting is on
//   music()        the pack's music setting is on
//   owns_music()   either is on: Morrowind's music script (files/data-mw/scripts/omw/music/music.lua) then stays out of
//                  the way, because its playlists name Morrowind's folders and the session has its own music or none.
// No engine type; the pack never reads a clock.

#include <openeden/game/bindings.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace game_skyrimse::presentation
{
    inline constexpr std::string_view kNamespace = "skyrimse.presentation";

    struct Flags
    {
        bool mMenus = false;
        bool mMusic = false;
        /// The music script gives way when the session has its own music or its own menus.
        [[nodiscard]] bool ownsMusic() const { return mMenus || mMusic; }
        /// The namespace is declared only when one of the two is on.
        [[nodiscard]] bool declared() const { return mMenus || mMusic; }
    };

    class PresentationService
    {
    public:
        explicit PresentationService(Flags flags);
        ~PresentationService();
        PresentationService(const PresentationService&) = delete;
        PresentationService& operator=(const PresentationService&) = delete;

        [[nodiscard]] const Flags& flags() const { return mFlags; }

        /// Declares the namespace; does nothing (ok) when neither flag is on.
        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Flags mFlags;
    };
}

#endif
