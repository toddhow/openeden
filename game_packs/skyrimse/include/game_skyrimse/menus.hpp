#ifndef OPENEDEN_GAME_SKYRIMSE_MENUS_H
#define OPENEDEN_GAME_SKYRIMSE_MENUS_H

// The loading screen tips of a Skyrim SE session (task 0207, plan section 4.24). Off unless [extension.skyrimse] menus.
//
// The main menu and the pause menu themselves are Lua on the RmlUi library (files/data/openeden/skyrimse/menus.lua,
// run by the menu script menus_menu.lua); this file is the pack's side of the loading screens: the LSCR records of the
// data and the script package `core.skyrimse.menus` (menuSafe, because the menu script is the one that hands the tip to
// the engine) that gives the next tip.
//
// Layout of an LSCR record, settled by the five masters
// (docs/tasks/0207-skyrim-menus-and-loading-screens.md section 2):
//  - EDID, DESC (the tip text: a string id into the plugin's .DLSTRINGS in a localised plugin), optional CTDA (and CIS1
//    and CIS2) conditions, NNAM (u32: a form id, the static the screen shows), SNAM (f32 scale), RNAM (3 x i16
//    rotation), ONAM (2 x i16 rotation limits), XNAM (3 x f32 position), MOD2 (the camera path nif).
//  - 298 records in Skyrim.esm, 3 in Update.esm, 19 in Dawnguard.esm, 4 in HearthFires.esm, 30 in Dragonborn.esm; every
//    one has all of DESC, NNAM, SNAM, RNAM, ONAM, XNAM; 252 of Skyrim.esm's 298 have CTDA conditions.
// What this pack uses: DESC and the conditions. The 3D object and the camera path (NNAM, MOD2 ...) are NOT used: the
// engine's loading screen draws no model (a tip is text only).
//
// What is ours (the data has no rule): a tip is chosen among the screens that have text and whose conditions hold, by a
// seeded pick over their form id order; the conditions are answered by the quest world of the dialogue data (the
// player's CURRENT state, not the destination's, because the destination is not known when the tip is chosen).
//
// Threading: the parse runs on the dialogue worker; everything else on the main thread. No engine type.

#include <game_skyrimse/journal.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>

#include <components/contentsource/plugin.hpp>

#include <openeden/game/bindings.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::menus
{
    inline constexpr std::string_view kNamespace = "skyrimse.menus";
    inline constexpr std::size_t kMaxScreens = 8192;
    inline constexpr std::size_t kMaxTipBytes = 1024; ///< a longer tip is cut at a character boundary

    struct LoadingScreen
    {
        RecordCommon mCommon;
        LString mText; ///< DESC
        FormId mObject = kNullFormId; ///< NNAM (not used)
        std::vector<survey::Ctda> mConditions;
        bool mConditionsOk = true; ///< false: a CTDA was damaged, the screen never passes
        std::uint32_t mPlugin = 0; ///< index in the load order of the plugin whose record won (for the string table)
    };

    struct LoadingScreenLimits
    {
        std::uint64_t mMaxRecordBytes = 1ull * 1024 * 1024;
        std::uint32_t mMaxConditions = 256;
        std::uint64_t mMaxScreens = kMaxScreens;
        std::uint32_t mMaxErrors = 256;
    };

    struct LoadingScreenRecords
    {
        std::string mName;
        std::vector<LoadingScreen> mScreens;
        std::uint64_t mFailed = 0;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::vector<Content::Error> mErrors;
        std::optional<Content::Error> mWalkError;
    };

    /// Reads the LSCR records of one plugin. False with `out.mWalkError` for a plugin that is not Skyrim SE's or whose
    /// walk failed.
    [[nodiscard]] bool parseLoadingScreens(
        const Content::Plugin& plugin, const LoadOrder& order, const LoadingScreenLimits& limits,
        LoadingScreenRecords& out);

    /// The screens of a load order: a later record of the same form id replaces an earlier one, a deleted one removes
    /// it. Add plugins in load order (the plugin index is the position among the calls).
    class LoadingScreenIndex
    {
    public:
        void add(LoadingScreenRecords&& records, FormIdResolver resolver);

        /// The screens in form id order. Valid until the next add().
        [[nodiscard]] std::vector<const LoadingScreen*> screens() const;
        [[nodiscard]] std::size_t size() const { return mScreens.size(); }
        [[nodiscard]] const FormIdResolver& resolverOf(const LoadingScreen& screen) const
        {
            return mResolvers[screen.mPlugin];
        }

    private:
        std::vector<FormIdResolver> mResolvers;
        std::vector<LoadingScreen> mScreens; // unordered; screens() sorts
    };

    /// What `pickTip` decides with. `mPasses` answers the conditions of a screen (it is called only for a screen that
    /// has text and whole conditions); `mText` resolves the screen's DESC.
    struct TipInputs
    {
        const LoadingScreenIndex* mScreens = nullptr;
        std::function<bool(const LoadingScreen&)> mPasses;
        std::function<std::string(const LoadingScreen&)> mText;
    };

    struct Tip
    {
        FormId mScreen = kNullFormId;
        std::string mText;
        std::size_t mCandidates = 0; ///< how many screens could have been chosen
    };

    /// A seeded pick among the candidates (text, whole conditions, conditions pass), taken in form id order. The same
    /// inputs and seed give the same tip. False when there is no candidate. The text is cut to kMaxTipBytes.
    [[nodiscard]] bool pickTip(const TipInputs& inputs, std::uint64_t seed, Tip& out);

    struct Access
    {
        const LoadingScreenIndex* mScreens = nullptr;
        /// Do these conditions hold now? `resolver` maps the stored form ids of the screen's own plugin.
        std::function<bool(const std::vector<survey::Ctda>&, const FormIdResolver&)> mEvaluate;
        journal::TextFn mText;
    };

    struct Hooks
    {
        std::function<bool(Access& out)> mAccess;
        std::function<void(const std::string&)> mLog;
    };

    /// The namespace `skyrimse.menus` 1.0 (menuSafe): `ready()` -> bool, `tip_count()` -> int, `tip(seed)` -> string
    /// (empty when not ready or no screen qualifies).
    class MenusService
    {
    public:
        explicit MenusService(Hooks hooks);
        ~MenusService();
        MenusService(const MenusService&) = delete;
        MenusService& operator=(const MenusService&) = delete;

        /// Asks the hook for the data until it is there; never waits.
        bool poll();
        [[nodiscard]] bool ready() const { return mReady; }
        [[nodiscard]] std::size_t screenCount() const;
        /// The tip for a seed (false when not ready or no screen qualifies).
        [[nodiscard]] bool tip(std::uint64_t seed, Tip& out);

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        Access mAccess;
        bool mReady = false;
    };
}

#endif
