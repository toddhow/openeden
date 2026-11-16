#ifndef OPENEDEN_GAME_SKYRIMSE_SHOUTS_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_SHOUTS_SERVICE_H

// The shouts service (task 0205): owns the player's ShoutState, applies the test settings once the records are read,
// answers the Papyrus natives (Game.TeachWord, UnlockWord, IsWordUnlocked) and declares the script namespace
// `skyrimse.shouts` 1.0 for the cooldown HUD (files/data/openeden/skyrimse/shouthud.lua):
//   Read   ready() -> bool, revision() -> int, equipped_name() -> string, cooldown() -> double,
//          cooldown_total() -> double, unlocked_count() -> int, word_count() -> int, souls() -> int
//   Mutate add_souls(n)
// Main thread only. No engine, renderer or script-language type.

#include <game_skyrimse/shouts.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace game_skyrimse::shouts
{
    struct TestSettings
    {
        std::string mLearn; ///< comma separated WOOP editor ids
        std::string mUnlock; ///< comma separated WOOP editor ids: learned, then unlocked with a soul each
        int mSouls = 0; ///< dragon souls given at the start
        std::string mShout; ///< the SHOU editor id equipped at the start
    };

    struct Hooks
    {
        /// The records, or null while they are being read.
        std::function<const ShoutIndex*()> mIndex;
        std::function<void(const std::string&)> mLog;
    };

    class ShoutService
    {
    public:
        explicit ShoutService(Hooks hooks);
        ~ShoutService();
        ShoutService(const ShoutService&) = delete;
        ShoutService& operator=(const ShoutService&) = delete;

        void setTestSettings(TestSettings settings);
        /// Once a frame: when the records are there, applies the test settings (once). True when they are applied.
        bool poll();
        [[nodiscard]] ShoutState& state();
        [[nodiscard]] const ShoutState& state() const;
        [[nodiscard]] ShoutChunk& chunk();

        /// The Papyrus natives. A word the records do not know is refused (false) and logged once.
        bool teachWord(FormId word);
        bool unlockWord(FormId word);
        [[nodiscard]] bool isWordUnlocked(FormId word) const;

        /// The equipped shout's name for the HUD (empty when none).
        [[nodiscard]] std::string equippedName() const;
        /// The leading unlocked words of the equipped shout.
        [[nodiscard]] std::size_t equippedWords() const;

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
