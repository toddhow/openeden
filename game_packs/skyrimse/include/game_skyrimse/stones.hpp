#ifndef OPENEDEN_GAME_SKYRIMSE_STONES_H
#define OPENEDEN_GAME_SKYRIMSE_STONES_H

// Standing stones and shrines (task 0237, milestone M-S6). Off unless `[extension.skyrimse] papyrus`.
//
// The stones and shrines are Skyrim's own scripts (`powerShrineScript`, `TempleBlessingScript`) running on the Papyrus
// host. This file is what the scripts' results need around them:
//  - `skillUseScale`: what a stone's ability does to skill experience. The Warrior, Mage, Thief and Lover stones'
//    effects have magnitude 0 and name a perk (MGEF DATA +136) whose entry point 22 (ModSkillUse) multiplies the use
//    of the skills its conditions name (VERIFIED on Skyrim.esm: the perks hold PRKE entry point 0x16, function 3,
//    EPFD 1.2 or 1.15, and CTDA function 681 with the skill's actor value in parameter 1);
//  - `supportOf`: which spells the runtime really applies to the player (Works), which it only records as active
//    (Partly) and which do nothing yet (Nothing);
//  - `StonesService`: the player's active spells (the host's persistent list plus the timed ones a shrine's Spell.Cast
//    gives), the message box a script asked (a stone asks "choose this sign?"), the save chunk `stones` and the script
//    namespace `skyrimse.messages` the Lua message window reads.
//
// Ours (RECALLED-VERIFY, tunable): a shrine blessing replaces the previous shrine blessing (spells whose editor id
// begins with "Altar"; the game's AltarRemoveMsg suggests it); a timed spell lasts the longest effect duration in game
// seconds (the data: 28,800, eight hours), counted on the host's game clock (script time at the time scale), which
// waiting and sleeping do not advance.
//
// Threading: main thread only. No engine, renderer or script-language type.

#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/perks.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::stones
{
    /// PERK entry point ModSkillUse (VERIFIED: PRKE entry point byte 0x16 in doomWarriorPerk) and the condition
    /// function that tests the skill being used (CTDA function 681; the name EPModSkillUsage_IsAdvanceSkill is
    /// RECALLED-VERIFY).
    inline constexpr std::uint8_t kEntryModSkillUse = 22;
    inline constexpr std::uint16_t kFunctionIsAdvanceSkill = 681;
    inline constexpr std::uint32_t kFirstSkillValue = 6; ///< the actor value of skill slot 0 (One-handed)
    /// Actor value indices of the two resists a stone gives (VERIFIED by MGEF DATA +68 of the Lord stone's effects).
    inline constexpr std::int32_t kValueDamageResist = 39;
    inline constexpr std::int32_t kValueMagicResist = 44;
    /// The most timed spells at once, and the longest a spell may last (a day of game time a hundred times).
    inline constexpr std::size_t kMaxTimedSpells = 32;
    inline constexpr double kMaxDurationSeconds = 86400.0 * 100.0;
    inline constexpr std::string_view kBlessingPrefix = "altar";

    /// The factor the experience of skill `slot` (0 to 17, the AVIF order) is multiplied by while the player has these
    /// spells: every effect's perk's ModSkillUse entries whose tab conditions name the skill (or name none), applied in
    /// spell id order (Set, Add, Multiply). 1 when nothing applies. Pure.
    [[nodiscard]] float skillUseScale(const PerkLibrary& library, std::span<const FormId> spells, std::size_t slot);

    enum class Support : std::uint8_t
    {
        Works, ///< every effect changes something the runtime reads
        Partly, ///< some effect does, the others are only kept as an active status
        Nothing, ///< no effect changes anything yet
    };
    [[nodiscard]] Support supportOf(const PerkLibrary& library, FormId spell, std::string* detail = nullptr);
    [[nodiscard]] std::string_view supportName(Support support);

    /// The longest effect duration of a spell in seconds (0: instant, or an ability that lasts for ever).
    [[nodiscard]] double durationOf(const PerkLibrary& library, FormId spell);

    class StonesService
    {
    public:
        struct Hooks
        {
            std::function<const PerkLibrary*()> mLibrary;
            /// The player's persistent spells (the host's Actor.AddSpell list), sorted.
            std::function<std::vector<FormId>()> mPersistent;
            /// The game clock in seconds.
            std::function<double()> mNow;
            /// The active spells changed: combat takes them as the player's extra abilities.
            std::function<void(std::span<const FormId>)> mApply;
            /// The player answered a message (the host runs the script again).
            std::function<bool(FormId message, std::int32_t button)> mAnswer;
            /// The player activated the placement of a base (the engine's resource event for a named activator): true
            /// when it is a stone or shrine and its script was told.
            std::function<bool(FormId placement, FormId base)> mActivate;
            std::function<void(const std::string&)> mLog;
            /// Task 0240: a spell cast on the player has a Cure Disease effect (a shrine's): the diseases go. May be
            /// empty.
            std::function<void()> mCure;
        };

        explicit StonesService(Hooks hooks);
        ~StonesService();
        StonesService(const StonesService&) = delete;
        StonesService& operator=(const StonesService&) = delete;

        /// A Spell.Cast on the player: a spell with a duration becomes a timed spell (a blessing replaces the previous
        /// one), an instant spell is only counted. False when the spell is unknown, the list is full or `target` is not
        /// the player.
        bool cast(FormId spell, FormId target);
        /// Once a frame: ends timed spells that ran out, and tells `mApply` when the active list changed.
        void poll();
        [[nodiscard]] std::vector<FormId> active() const; ///< persistent and timed, sorted, no duplicates
        [[nodiscard]] std::vector<FormId> timed() const; ///< sorted
        [[nodiscard]] float skillScale(std::size_t slot) const;
        [[nodiscard]] std::uint64_t revision() const;

        // ---- the message box a script asked
        void ask(FormId message, const PapyrusMessageInfo& info);
        [[nodiscard]] bool messageOpen() const;
        [[nodiscard]] const std::string& messageText() const;
        [[nodiscard]] const std::vector<std::string>& messageButtons() const;
        /// A button (0-based) of the open message; false when none is open or the button does not exist.
        bool choose(std::int32_t button);
        [[nodiscard]] std::size_t answered() const;
        /// The Mutate binding `activate(placement, base)` (hex texts): a real-key activation of a stone or shrine.
        bool activate(FormId placement, FormId base);

        /// The Lua namespace `skyrimse.messages` 1.0 (Read: active, text, button_count, button, revision; Mutate:
        /// choose(button, 1-based), activate(placement, base) as hex texts).
        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        /// The chunk "stones" (version 1): the timed spells and when they end. Strict: a damaged or newer chunk is
        /// refused and changes nothing.
        [[nodiscard]] openeden::sdk::SaveChunkHandler& chunk();

        struct Timed
        {
            FormId mSpell = 0;
            double mEnds = 0.0;
        };
        [[nodiscard]] const std::vector<Timed>& timedSpells() const { return mTimed; }
        void restoreTimed(std::vector<Timed> spells);

    private:
        struct Impl;
        void log(const std::string& line) const;
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        std::vector<Timed> mTimed;
        std::vector<FormId> mAppliedSpells;
        bool mApplied = false;
        std::uint64_t mRevision = 1;
        FormId mMessage = 0;
        std::string mText;
        std::vector<std::string> mButtons;
        std::size_t mAnswered = 0;
    };
}

#endif
