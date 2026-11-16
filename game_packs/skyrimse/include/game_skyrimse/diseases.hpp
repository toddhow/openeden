#ifndef OPENEDEN_GAME_SKYRIMSE_DISEASES_H
#define OPENEDEN_GAME_SKYRIMSE_DISEASES_H

// Diseases (task 0240, milestone M-S7). Off unless `[extension.skyrimse] diseases` (and, for a creature to carry one,
// `[Game] neutral combat` and the pack setting `creatures`).
//
// What the data says (the five masters; docs/tasks/0240-skyrim-diseases.md section 1):
//  - VERIFIED  a disease is a SPEL whose SPIT +8 (type) is 1: 16 records (Ataxia, BoneBreakFever, BrainRot,
//              SanguinareVampiris, Rattles, Rockjoint, Witbane, six trap copies, BlackHeartBlight, Droops), all constant
//              effects (cast type 0) with duration 0, i.e. they last until cured.
//  - VERIFIED  a race's attacks name their spell in RACE ATKD +8 (44 bytes each): the wolf, fox, skeever, chaurus, bear,
//              sabre cat, hagraven, ash hopper ... name a disease. ATKD +4 is the AI's weight for choosing the attack
//              (the NordRace's unarmed attacks carry 0.0 to 1.0 with no spell), NOT an infection chance.
//  - VERIFIED  actor value 45 is the disease resistance (the MGEF AbResistDisease; Argonian and Wood Elf abilities 50,
//              the werewolf, vampire and werebear abilities 100); archetype 3 is Cure Disease (CureDiseaseEffect on all
//              the shrine spells, AlchCureDisease on the Cure Disease potion, garlic bread, the werewolf and vampire cures).
//  - NOT IN THE DATA  the chance a landed blow infects: no GMST holds it. The chance below is ours (RECALLED-VERIFY).
//
// What a disease does in this runtime (modifiersOf): the maximum and regeneration of stamina and magicka, and a weapon
// skill's damage percent. Health maximum (Sanguinare), carry weight (Black-Heart Blight) and the lockpicking and
// pickpocket modifiers (Ataxia) are carried as active effects and change nothing yet.
//
// Standard library, the SDK bindings and save chunk types, and the pack's own data headers only. Main thread.

#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/perks.hpp>
#include <game_skyrimse/value_modifiers.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::diseases
{
    /// MGEF DATA +64 archetype 3 (VERIFIED by the names of the three effects that have it).
    inline constexpr std::uint32_t kArchetypeCureDisease = 3;
    /// Actor values (MGEF DATA +68 of the named effects: DisDamageStamina 26, DisDamageMagicka 25, DisDamageStaminaRegen
    /// 29, DisDamageMagickaRegen 28, DisDamageHealthVampire 24, DLC2DisDamageCarryWeight 32, AbResistDisease 45).
    inline constexpr std::int32_t kValueHealth = 24, kValueMagicka = 25, kValueStamina = 26, kValueMagickaRate = 28,
                                  kValueStaminaRate = 29, kValueCarryWeight = 32, kValueResistDisease = 45;
    inline constexpr std::int32_t kFirstSkillModifier = 96; ///< 96 + the skill slot (One-handed 96, Two-handed 97 ...)
    inline constexpr std::int32_t kSkillModifierCount = 18;

    /// OURS (RECALLED-VERIFY, tunable): the chance that a landed, unblocked blow from a carrier infects a player with no
    /// resistance, and the seed of the rolls.
    inline constexpr float kDefaultChance = 0.10f;
    inline constexpr std::uint64_t kDefaultSeed = 0x0D15EA5E5ull;

    inline constexpr std::size_t kMaxDiseases = 16; ///< the data has 16
    inline constexpr std::size_t kMaxNotifications = 16;
    inline constexpr std::size_t kMaxNameBytes = 80;

    struct Settings
    {
        float mChance = kDefaultChance;
        std::uint64_t mSeed = kDefaultSeed;
    };

    /// The name shown for a disease: its editor id without the Trap, DLC2 and Disease prefixes, split at capitals
    /// ("DiseaseBoneBreakFever" to "Bone Break Fever"). The SPEL's own FULL is not read (decision for toddhow).
    [[nodiscard]] std::string displayName(std::string_view editorId);

    /// Does one of the spell's effects have the Cure Disease archetype?
    [[nodiscard]] bool hasCureDisease(const PerkLibrary& library, FormId spell);
    /// Is this MGEF a Cure Disease effect (a potion's effect list is a list of MGEF ids)?
    [[nodiscard]] bool isCureDiseaseEffect(const PerkLibrary& library, FormId effect);

    /// What a set of diseases changes (signed: a detrimental effect takes away). Effects that change nothing here are
    /// described in `ignored` (one text each) when it is not null.
    [[nodiscard]] PlayerValueModifiers modifiersOf(
        const PerkLibrary& library, std::span<const FormId> diseases, std::vector<std::string>* ignored = nullptr);

    /// Task 0248: the same, and also the effects the older rule leaves described only: the health maximum (Sanguinare
    /// Vampiris, actor value 24), the carry weight (Black-Heart Blight, 32) and every skill modifier slot (Ataxia: 104
    /// lockpicking and 103 pickpocket). `modifiersOf` is unchanged (tests pin what it ignores).
    [[nodiscard]] PlayerValueModifiers modifiersOfExtended(
        const PerkLibrary& library, std::span<const FormId> diseases, std::vector<std::string>* ignored = nullptr);

    /// The sum of the disease resistance (actor value 45) the ability spells give, in percent (not clamped).
    [[nodiscard]] float resistanceOf(const PerkLibrary& library, std::span<const FormId> spells);

    /// The chance a blow infects: the base chance times (1 - resistance / 100), both clamped to 0 .. 1 / 0 .. 100.
    [[nodiscard]] float infectionChance(float baseChance, float resistPercent);

    /// A number in [0, 1) from the seed and the roll's number, deterministic (splitmix64).
    [[nodiscard]] float rollUnit(std::uint64_t seed, std::uint64_t counter);

    /// The first disease among a race's attack spells, or 0.
    [[nodiscard]] FormId diseaseOfAttacks(const PerkLibrary& library, std::span<const FormId> attackSpells);

    class DiseaseService
    {
    public:
        struct Hooks
        {
            std::function<const PerkLibrary*()> mLibrary;
            /// The spells the player has now (abilities among them), for his resistance.
            std::function<std::vector<FormId>()> mPlayerSpells;
            /// The player's numbers changed: combat applies them (SkyrimCombat::setPlayerValueModifiers).
            std::function<void(const PlayerValueModifiers&)> mApply;
            std::function<void(const std::string&)> mLog;
        };

        enum class HitResult : std::uint8_t
        {
            NotReady, ///< no record library yet
            NoDisease, ///< the attacker's attacks carry none
            Already, ///< the player has it
            Immune, ///< resistance 100 percent or more: no roll is spent
            Missed, ///< rolled and not infected
            Infected,
        };

        DiseaseService(Hooks hooks, Settings settings);
        ~DiseaseService();
        DiseaseService(const DiseaseService&) = delete;
        DiseaseService& operator=(const DiseaseService&) = delete;

        /// A blow that landed on the player from a fighter whose attacks carry `attackSpells`.
        HitResult onHit(std::span<const FormId> attackSpells, std::uint32_t attacker);
        /// Infects with a disease (a test aid and the shrine-free path); false when it is not a disease, is already held,
        /// the list is full or the library is not ready. Raises the notification.
        bool infect(FormId disease);
        /// Cures every disease; returns how many went. A notification each.
        std::size_t cure();
        /// Task 0260: cures one disease (Sanguinare Vampiris at the turn); `announce` false leaves out the "cured"
        /// notification. False when the player does not have it.
        bool cureOne(FormId disease, bool announce);
        /// Once a frame: gives combat the modifiers when they changed (or were never sent).
        void poll();

        [[nodiscard]] const std::vector<FormId>& infected() const { return mInfected; }
        [[nodiscard]] bool has(FormId disease) const;
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }
        [[nodiscard]] std::uint64_t rolls() const { return mRolls; }
        [[nodiscard]] std::string nameOf(FormId disease) const;
        [[nodiscard]] std::uint64_t notificationCount() const { return mNotificationCount; }
        [[nodiscard]] std::string notificationAt(std::uint64_t number) const; ///< 1-based, "" when out of the ring
        [[nodiscard]] const Settings& settings() const { return mSettings; }
        void setSettings(const Settings& settings) { mSettings = settings; }

        /// The Lua namespace `skyrimse.diseases` 1.0 (Read: count, name(n), revision, notification_count,
        /// notification(n)).
        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

        /// The chunk "diseases" (version 1): the infected spells and the number of rolls. Strict.
        [[nodiscard]] openeden::sdk::SaveChunkHandler& chunk();
        /// What a chunk read leaves: replaces the list and the roll counter, bumps the revision.
        void restore(std::vector<FormId> infected, std::uint64_t rolls);

    private:
        struct Impl;
        void log(const std::string& line) const;
        void note(std::string text);
        std::unique_ptr<Impl> mImpl;
        Hooks mHooks;
        Settings mSettings;
        std::vector<FormId> mInfected;
        std::uint64_t mRevision = 1;
        std::uint64_t mSentRevision = 0;
        std::uint64_t mRolls = 0;
        std::uint64_t mNotificationCount = 0;
        std::vector<std::string> mNotifications; ///< a ring of kMaxNotifications
    };
}

#endif
