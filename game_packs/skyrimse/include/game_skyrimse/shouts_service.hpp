#ifndef OPENEDEN_GAME_SKYRIMSE_SHOUTS_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_SHOUTS_SERVICE_H

// The shouts service (task 0205): owns the player's ShoutState, applies the test settings once the records are read,
// answers the Papyrus natives (Game.TeachWord, UnlockWord, IsWordUnlocked) and declares the script namespace
// `skyrimse.shouts` 1.0 for the cooldown HUD (files/data/openeden/skyrimse/shouthud.lua):
//   Read   ready() -> bool, revision() -> int, equipped_name() -> string, cooldown() -> double,
//          cooldown_total() -> double, unlocked_count() -> int, word_count() -> int, souls() -> int
//   Mutate add_souls(n), unlock_word(name) (task 0256: a dragon soul spent on a learned word, by its editor id)
// Main thread only. No engine, renderer or script-language type.

#include <game_skyrimse/shouts.hpp>

#include <openeden/game/bindings.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
        /// Task 0266 (optional): the text of a localised string of the record `owner` (the shout menu's word names and
        /// translations). Empty when it cannot be read; without the hook the menu shows the editor id's name.
        std::function<std::string(const LString&, FormId owner)> mText;
    };

    /// Task 0266: one word of a shout in the menu. mState is "unlocked", "learned" (known, not unlocked) or "unknown".
    struct MenuWord
    {
        std::string mName;
        std::string mTranslation;
        std::string mState;
    };

    /// Task 0266: a shout the player has learned at least one word of.
    struct MenuShout
    {
        std::string mId; ///< the editor id, what the mutating calls take
        std::string mName;
        bool mEquipped = false;
        bool mCanEquip = false; ///< at least one word is unlocked
        bool mCanUnlock = false; ///< the next word is learned and a dragon soul is there
        std::vector<MenuWord> mWords;
    };

    enum class MenuResult
    {
        Done,
        NoShout, ///< no such shout, or the records are not read
        NotLearned, ///< the next word has not been learned (a word wall)
        NoSoul,
        AllUnlocked,
        NothingUnlocked, ///< to equip: no word of the shout is unlocked
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
        /// Task 0256: unlocks a LEARNED word, named by its editor id ("WordFus"), by spending one dragon soul (the
        /// shout menu does not exist yet; the script namespace's `unlock_word` calls this). False, and nothing spent,
        /// when the word is unknown, not learned, already unlocked or there is no soul.
        bool unlockWithSoul(std::string_view wordEditorId);

        /// Task 0266 (the shout menu): the shouts with a learned word, by form id, with their words' state. Empty
        /// before the records are read.
        [[nodiscard]] std::vector<MenuShout> menu() const;
        /// Spends a dragon soul on the shout's next word (the first one after its unlocked run) when it is learned. The
        /// result is announced to the player as a message.
        MenuResult menuUnlockNext(std::string_view shoutEditorId);
        /// Makes the shout the one the shout key uses (needs one unlocked word). Announced as a message.
        MenuResult menuEquip(std::string_view shoutEditorId);

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
