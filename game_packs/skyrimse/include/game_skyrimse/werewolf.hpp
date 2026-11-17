#ifndef OPENEDEN_GAME_SKYRIMSE_WEREWOLF_H
#define OPENEDEN_GAME_SKYRIMSE_WEREWOLF_H

// Task 0259 (the werewolf beast form, first slice): the pure half. The beast clock (how long the form lasts, the
// warning, what feeding adds), the profile the player's werewolf body is drawn and fights with, and the small rules
// around them. No engine, renderer, script-language or clock type: every function takes the seconds it is given.
//
// Where each number comes from (the survey is docs/tasks/0259-skyrim-werewolf.md section 1):
//   VERIFIED  the quest PlayerWerewolfQuest's script PlayerWerewolfChangeScript (decompiled from Skyrim - Misc.bsa) and
//             its VMAD properties: StandardDurationSeconds 150, DurationWarningTimeSeconds 15, FeedExtensionTimeSeconds
//             30 (half of it for any victim, the other half only for a person); the race WerewolfBeastRace 0xCDD84 and
//             its skin NakedWerewolfBeast 0xCDD86 -> NakedTorsoWerewolfBeast 0xCDD87 (one model for both sexes); the
//             power WerewolfChange 0x92C48 whose only effect WerewolfChangeEffect 0x92C45 has archetype 36 (MGEF DATA
//             +64); the abilities AbWerewolf 0xA1A3E and PlayerWerewolfLvlNN; the feed spell WerewolfFeed 0xEC357
//             (magnitude 50).
//   OURS      the cap on the clock, treating the clock as pack seconds (the script counts game days through the time
//             scale), the instant health of a feed, the capsule size.
// Off unless the pack setting `werewolf` is on (with `creatures`, `player body` and the engine's `[Game] neutral
// actors`).

#include <game_skyrimse/creatures.hpp>
#include <game_skyrimse/records.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace game_skyrimse::werewolf
{
    // ---- settings ---------------------------------------------------------------------------------------------------

    /// The pack setting (bool, default false): the player can become a werewolf.
    inline constexpr std::string_view kKey = "werewolf";
    /// A developer aid (bool, default false): the player has the power Beast Form from the start (the real way is the
    /// Companions quest line, which is long).
    inline constexpr std::string_view kTestGrantKey = "werewolf test grant";
    /// A developer aid: seconds the form lasts (0 keeps the 150 of the quest script).
    inline constexpr std::string_view kTestSecondsKey = "werewolf test seconds";

    // ---- records ----------------------------------------------------------------------------------------------------

    inline constexpr FormId kBeastRace = 0xCDD84; ///< WerewolfBeastRace
    inline constexpr FormId kBeastPower = 0x92C48; ///< SPEL WerewolfChange, the power "Beast Form"
    inline constexpr FormId kBaseAbility = 0xA1A3E; ///< SPEL AbWerewolf (health, stamina, disease resistance, ...)
    inline constexpr std::uint32_t kArchetypeWerewolf = 36; ///< MGEF DATA +64 of WerewolfChangeEffect

    /// The ability PlayerWerewolfLvlNN (PlayerWerewolfChangeScript.StartTracking) for a player level: 10, 15, 20, 25,
    /// 30, 35, 40, 45 and "50 and over" give 0x10C6E8, 0x10F186, 0x10C6E9, 0x10F187, 0x10C6EA, 0x10F188, 0x10F189,
    /// 0x10F18A, 0x10C6EB. Level 0 counts as 1.
    [[nodiscard]] FormId levelAbility(std::uint32_t playerLevel);

    // ---- the clock --------------------------------------------------------------------------------------------------

    inline constexpr float kStandardSeconds = 150.f; ///< VERIFIED
    inline constexpr float kWarnSeconds = 15.f; ///< VERIFIED
    inline constexpr float kFeedSeconds = 30.f; ///< VERIFIED: half for an animal victim, all of it for a person
    /// The clock never holds more than this (ours): feeding cannot make the form last for ever.
    inline constexpr float kMaxSeconds = 600.f;
    /// The health a feed restores at once (WerewolfFeed's effect magnitude; the real effect is not instant: ours).
    inline constexpr float kFeedHealth = 50.f;

    class BeastClock
    {
    public:
        enum class Event : std::uint8_t
        {
            None,
            Warned, ///< the time left just fell to the warning (once per stretch)
            Ended, ///< the time ran out
        };

        /// Starts the form with `seconds` left (clamped to (0, kMaxSeconds]; a bad number gives the standard 150).
        void start(float seconds);
        /// Ends it now (death, a script) without an event.
        void stop();
        /// A frame of `dt` seconds (a bad or negative dt counts as 0, one over a second as a second).
        Event tick(float dt);
        /// Feeding adds time: half of kFeedSeconds for any victim and the other half for a person. False when the form
        /// is not on.
        bool feed(bool victimIsPerson);

        [[nodiscard]] bool active() const { return mActive; }
        [[nodiscard]] float remaining() const { return mActive ? mRemaining : 0.f; }
        [[nodiscard]] bool warned() const { return mWarned; }

    private:
        bool mActive = false;
        bool mWarned = false;
        float mRemaining = 0.f;
    };

    /// What the claws do: the race's bare-hand numbers plus the level ability's Fortify Unarmed Damage, and the
    /// race's sounds (RACE NAM4 body material, NAM5 bare-hand impact set).
    struct BeastNumbers
    {
        float mUnarmedDamage = 0.f;
        float mUnarmedReach = 0.f;
        FormId mBodyMaterial = kNullFormId;
        FormId mUnarmedImpactSet = kNullFormId;
    };
    /// The MGEF AbFortifyUnarmedDamage that the level abilities carry (VERIFIED: PlayerWerewolfLvl10AndBelowAbility's
    /// first effect, magnitudes 0, 5, ..., 25, 30, 50, 60 over the levels).
    inline constexpr FormId kFortifyUnarmedEffect = 0x424E2;

    // ---- the body ---------------------------------------------------------------------------------------------------

    /// The beast's clips live in another skeleton's folder than the human clips the core asks the timing of by default,
    /// so a clip name may carry its creature folder in front: `werewolfbeast::ww_attackright`. The folder names the
    /// sidecar too (`<folder>project`).
    inline constexpr std::string_view kCreatureFolder = "werewolfbeast";
    inline constexpr std::string_view kClipSeparator = "::";

    /// The clip a name names with its creature folder taken off (`werewolfbeast::ww_x` gives `ww_x`; a name with no
    /// separator is returned as it is).
    [[nodiscard]] std::string_view bareClip(std::string_view clip);
    /// The folder in front of a name (`werewolfbeast`), or empty.
    [[nodiscard]] std::string_view clipFolder(std::string_view clip);

    /// The beast's creature row for the PLAYER: the clip names of its attacks carry the folder (see above), because the
    /// pack asks the core for their timing as for any player clip. The idle, walk and run names are bare (the body's
    /// own controller finds them in the skeleton's folder).
    [[nodiscard]] const CreatureProfile& playerProfile();
    /// The same row with bare clip names (what an NPC werewolf's table entry would hold).
    [[nodiscard]] const CreatureProfile& bareProfile();

    // ---- saving (task 0265) -----------------------------------------------------------------------------------------

    /// What a save keeps of the beast form: whether Beast Form has been granted to the player, whether the form is on,
    /// the seconds left on the clock and the corpses fed on in this form (placement ids). When the last use of the
    /// power (once a game day) is saved too: by the character service's `character_powers` chunk, which already keys
    /// it by the power's form id, so it is not repeated here.
    struct BeastSaved
    {
        bool mGranted = false;
        bool mActive = false;
        float mRemaining = 0.f;
        std::vector<std::uint64_t> mFed;
    };
    inline constexpr std::size_t kMaxSavedFed = 4096; ///< hostile-input cap on the fed list

    /// The chunk "werewolf" version 1: u8 granted (0/1), u8 active (0/1), f32 seconds left, u32 count, count x u64.
    /// Canonical: the fed list is sorted and has no repeats, `mRemaining` is a finite number in (0, kMaxSeconds] when
    /// the form is on and 0 when it is not (a form on with no time left is refused, so is a damaged or longer chunk).
    [[nodiscard]] std::vector<std::uint8_t> encodeBeastSaved(const BeastSaved& saved);
    [[nodiscard]] bool decodeBeastSaved(const std::uint8_t* data, std::size_t size, BeastSaved& out);

    /// The save chunk handler: asks `get` at a save and hands what it read to `set` (only a chunk that read cleanly
    /// reaches `set`; a bad one changes nothing). Registered only while the werewolf setting is on, so a save made
    /// without it has no such chunk and an older save simply never calls it.
    [[nodiscard]] std::unique_ptr<openeden::sdk::SaveChunkHandler> makeBeastChunk(
        std::function<BeastSaved()> get, std::function<void(const BeastSaved&)> set);
}

#endif
