#ifndef OPENEDEN_GAME_SKYRIMSE_CHARACTER_H
#define OPENEDEN_GAME_SKYRIMSE_CHARACTER_H

// Task 0225: a first character creation for a Skyrim SE session. Pure: no engine, UI or script type, no clock, no
// random numbers, nothing throws. This file is the model of the choices (the playable races, the head parts a race and
// sex offer, the working choice with its rules); `character_service.*` is the service that keeps the choice, saves it
// and declares the script namespace.
//
// SETTLED BY THE DATA (the five masters, a Python walker; aggregates in docs/tasks/0225-skyrim-character-creation.md):
//  * RACE DATA offset 32 is a flags word whose bit 0 is "playable": it is set on exactly 10 of the 161 RACE records,
//  the
//    ten player races (VERIFIED: the Nord, Imperial, Breton, Redguard, Dark Elf, High Elf, Wood Elf, Orc, Khajiit and
//    Argonian forms 0x13740 to 0x13749).
//  * RACE DATA bytes 0 to 13 are seven (actor value, boost) pairs; the actor value 255 ends the list. VERIFIED against
//    the game's known starting bonuses (Nord: Two-Handed 10, One-Handed, Block, Smithing, Speech, Light Armor 5; Wood
//    Elf: Archery 10, Sneak, Lockpicking, Pickpocket, Light Armor, Alchemy 5).
//  * RACE SPLO are the race's abilities and powers (Nord: Resist Frost 0xAA020 and a power 0xE40C3).
//  * HDPT DATA flags: bit 0 playable, bit 1 male, bit 2 female, bit 3 "extra part" (a part another part brings along);
//    HDPT PNAM is the type (2 eyes, 3 hair, 4 facial hair, 6 brows ...); HDPT RNAM is an FLST of the races it is valid
//    for (9 of 766 have none). For the Nord man that gives 22 eyes, 29 hairs and 46 beards, for the Nord woman 20 eyes
//    and 26 hairs and no beard.
// What is OURS (the data has no rule): the three parts the menu offers (hair, facial hair, eyes), "None" for hair and
// facial hair, that a change of race or sex goes back to that race's defaults, the order of the lists (editor id) and
// the default name.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/journal.hpp>
#include <game_skyrimse/player_body.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::character
{
    /// `[extension.skyrimse] character creation` (default false; needs `menus` and `player body`).
    inline constexpr std::string_view kSettingKey = "character creation";
    inline constexpr std::string_view kNamespace = "skyrimse.character";
    inline constexpr std::size_t kMaxNameBytes = 32;
    inline constexpr std::size_t kMaxRaces = 32;
    inline constexpr std::size_t kMaxOptionsPerPart = 512;
    /// OURS: the name of a player who left the field empty.
    inline constexpr std::string_view kDefaultName = "Dragonborn";

    /// The head parts the menu lets the player change.
    enum class Part : std::uint8_t
    {
        Hair,
        Beard,
        Eyes,
    };
    inline constexpr std::size_t kPartCount = 3;

    /// The HDPT type (PNAM) of a part: Hair 3, Beard (facial hair) 4, Eyes 2.
    [[nodiscard]] std::uint32_t headPartType(Part part);
    /// "hair", "beard" or "eyes".
    [[nodiscard]] std::string_view partName(Part part);
    [[nodiscard]] bool partFromName(std::string_view name, Part& out);

    struct RaceOption
    {
        FormId mId = kNullFormId;
        std::string mEditorId;
        std::string mName; ///< FULL, else the editor id
        std::string mDescription; ///< DESC, may be empty
        std::vector<RaceSkillBoost> mBoosts; ///< the race's starting skill bonuses
        std::vector<FormId> mAbilities; ///< SPLO
    };

    struct PartOption
    {
        FormId mId = kNullFormId; ///< kNullFormId is "None"
        std::string mName;
    };

    /// What the player chose. The default is the Nord man the body of task 0222 had as constants.
    struct Choice
    {
        FormId mRace = kPlayerRace;
        bool mFemale = false;
        std::array<FormId, kPartCount> mParts{}; ///< indexed by Part; kNullFormId is "none"
        std::string mName;

        bool operator==(const Choice&) const = default;
    };

    using TextFn = journal::TextFn;

    /// The playable races (RACE DATA flag 0 and a skeleton), sorted by name (case-insensitive) then id, at most
    /// kMaxRaces. `text` resolves FULL and DESC (an empty function reads only text that is not localised).
    [[nodiscard]] std::vector<RaceOption> playableRaces(const ActorIndex& index, const TextFn& text);

    /// The parts of a kind a race and sex offer: playable, not an extra part, of the right sex, the right HDPT type
    /// and with the race in its valid-race list; hair and facial hair start with "None"; the race's own default of the
    /// kind is always among them. Sorted by editor id, at most kMaxOptionsPerPart.
    [[nodiscard]] std::vector<PartOption> partOptions(
        const ActorIndex& index, const TextFn& text, FormId race, bool female, Part part);

    /// The race's own default part of a kind for the sex (kNullFormId when it has none).
    [[nodiscard]] FormId defaultPart(const ActorIndex& index, FormId race, bool female, Part part);

    /// The race's default choice for a sex (its default hair, facial hair and eyes); the name is kept empty.
    [[nodiscard]] Choice defaultChoice(const ActorIndex& index, FormId race, bool female);

    /// A choice the pack can build a body from: a playable race with a skeleton, and every chosen part a valid option
    /// of that race and sex. `why` says what is wrong.
    [[nodiscard]] bool validChoice(const ActorIndex& index, const Choice& choice, std::string& why);

    /// A name the player may have: printable ASCII only, leading and trailing spaces cut, at most kMaxNameBytes.
    [[nodiscard]] std::string sanitiseName(std::string_view name);

    /// Puts the choice into a body request (race, sex, the head parts of the three kinds).
    void applyChoice(const Choice& choice, PlayerBodyRequest& request);

    /// The save blob of a choice: version, race, sex, the parts, the name. Canonical (the same choice, the same bytes).
    [[nodiscard]] std::vector<std::uint8_t> encodeChoice(const Choice& choice);
    /// False with `error` for a damaged or newer blob; `out` is untouched then.
    [[nodiscard]] bool decodeChoice(std::span<const std::uint8_t> bytes, Choice& out, std::string& error);

    /// "Two-Handed +10, One-Handed +5": a race's boosts as text, in the record's order.
    [[nodiscard]] std::string describeBoosts(const std::vector<RaceSkillBoost>& boosts);

    /// The working choice of the menu. Built over the actor index (which must outlive it) once it is read.
    class Creator
    {
    public:
        Creator(const ActorIndex& index, TextFn text);

        [[nodiscard]] bool ok() const { return !mRaces.empty(); }
        [[nodiscard]] const std::vector<RaceOption>& races() const { return mRaces; }
        [[nodiscard]] std::size_t raceIndex() const { return mRaceIndex; }
        [[nodiscard]] const RaceOption* race() const
        {
            return mRaceIndex < mRaces.size() ? &mRaces[mRaceIndex] : nullptr;
        }
        [[nodiscard]] const Choice& choice() const { return mChoice; }
        [[nodiscard]] bool female() const { return mChoice.mFemale; }
        [[nodiscard]] const std::vector<PartOption>& options(Part part) const
        {
            return mOptions[static_cast<std::size_t>(part)];
        }
        [[nodiscard]] std::size_t partIndex(Part part) const;
        /// Bumped by every change of the choice.
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }

        /// Chooses a race by its index in races(): the parts go back to its defaults (the sex stays). False for a bad
        /// index.
        bool selectRace(std::size_t index);
        /// Chooses the sex: the parts go back to the race's defaults for it.
        void setFemale(bool female);
        /// Moves a part's choice by `delta` places in its option list, wrapping round.
        void stepPart(Part part, int delta);
        /// Sets the name (sanitised).
        void setName(std::string_view name);
        /// Takes a saved choice. False (nothing changes) when it is not valid.
        bool load(const Choice& choice, std::string& why);

    private:
        void rebuildOptions();

        const ActorIndex& mIndex;
        TextFn mText;
        std::vector<RaceOption> mRaces;
        std::size_t mRaceIndex = 0;
        Choice mChoice;
        std::array<std::vector<PartOption>, kPartCount> mOptions;
        std::uint64_t mRevision = 1;
    };
}

#endif
