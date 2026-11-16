#ifndef OPENEDEN_GAME_SKYRIMSE_SUMMONS_H
#define OPENEDEN_GAME_SKYRIMSE_SUMMONS_H

// Task 0250: summons and raise dead. The pure part of the rules: the MGEF archetypes, the ledger of what a caster has
// conjured or raised, and the small geometry of where it appears. Standard library and the SDK's combat types only; no
// clock (the caller passes the combat clock), no records, nothing allocates after construction.
//
// What the data settles (the five masters, `SkyrimSummonsReal`): MGEF DATA +64 is 18 on every Summon Creature effect
// (Conjure Familiar, Flame Atronach, Frost Atronach, Storm Atronach, Dremora Lord, ...) and 22 on Reanimate (Raise
// Zombie, Revenant, Dead Thrall); +8 is the associated form: an NPC_ or a leveled NPC list for a summon. The archetype
// numbers agree with CommonLibSSE-NG's list; what each effect DOES is RECALLED-VERIFY and ours below:
//  - a summon appears 150 units in front of the caster, lives for the effect's duration and goes at once when it ends;
//  - one conjured thing at a time per caster (a new one dismisses the old), the count a setting, perks do not raise it;
//  - a reanimated corpse is raised at its own place (a new actor of the same base: the corpse is hidden), lives for the
//    effect's duration and then falls dead again where it stands; the magnitude is the highest level it may be.

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// `[extension.skyrimse] summons` (off) turns the effects on (it needs `combat spell delivery`); `summons max` is how
    /// many things one caster keeps (default kDefaultMaxSummons).
    inline constexpr std::string_view kSummonsKey = "summons";
    inline constexpr std::string_view kSummonsMaxKey = "summons max";
    inline constexpr std::string_view kSummonsTestKillKey = "summons test kill";
    inline constexpr std::string_view kSummonsTestMagickaKey = "summons test magicka";
    /// Combat-clock seconds after the data was read at which `summons test kill` kills its actor.
    inline constexpr float kSummonTestKillSeconds = 3.f;

    using SummonFormId = std::uint32_t;

    /// MGEF DATA +64 of the summoning effects (CommonLibSSE-NG ArchetypeID; the data agrees: see the real-data test).
    namespace SummonArchetype
    {
        inline constexpr std::uint32_t CommandSummoned = 10, SummonCreature = 18, Reanimate = 22;
    }

    /// The most conjured or raised things one caster keeps at once (OURS; Skyrim allows two with a perk).
    inline constexpr std::size_t kDefaultMaxSummons = 1;
    /// The ledger never holds more than this across all casters.
    inline constexpr std::size_t kMaxSummonEntries = 64;
    /// Where a summon appears: this far in front of the caster and this far above its feet (OURS; it falls to the floor).
    inline constexpr double kSummonSpawnDistance = 150.0;
    inline constexpr double kSummonSpawnRise = 8.0;
    /// A summon with no duration lives this long (a duration of 0 in the data would mean "instant").
    inline constexpr float kDefaultSummonSeconds = 60.f;
    /// A conjured thing that was killed stays this long (combat clock seconds), then vanishes.
    inline constexpr float kSummonDeadLinger = 2.f;
    /// A corpse is raised only when it lies within this of the caster and within this cosine of the caster's heading.
    inline constexpr double kReanimateReach = 1500.0;
    inline constexpr double kReanimateMinDot = 0.3;

    enum class SummonKind : std::uint8_t
    {
        Summoned, ///< a creature that was not there (Summon Creature)
        Reanimated, ///< a corpse that was (Reanimate)
    };

    struct SummonEntry
    {
        openeden::sdk::game::WorldId mReference = openeden::sdk::game::kNoWorldId; ///< the placement of the new actor
        openeden::sdk::game::CombatSubject mMaster;
        SummonFormId mSpell = 0;
        SummonFormId mEffect = 0;
        SummonFormId mBase = 0; ///< the NPC_ that was placed
        SummonKind mKind = SummonKind::Summoned;
        float mExpiresAt = 0.f; ///< the combat clock
        /// A Reanimated entry: the placement of the corpse that was hidden (kNoWorldId for a Summoned one).
        openeden::sdk::game::WorldId mCorpse = openeden::sdk::game::kNoWorldId;
        bool mArrived = false; ///< the core told the pack about the actor
        openeden::sdk::game::ActorHandle mHandle; ///< valid once it arrived
        float mDeadAt = -1.f; ///< the combat clock when it was seen dead (a Summoned one is removed kSummonDeadLinger later)
    };

    /// What the pack asks of the world; each is set by the glue that owns the world source service. A missing hook
    /// makes the effect do nothing (counted, traced). All main thread.
    struct SummonHooks
    {
        /// The NPC_ form to place for the effect's associated form (an NPC_ itself, or a leveled list picked for
        /// `level` by `pick`); 0 when it names nothing placeable.
        std::function<SummonFormId(SummonFormId associated, std::uint16_t level, std::uint32_t pick)> resolveBase;
        /// Places an actor of `base` at the position (heading as the engine stores it, 0 faces +Y); the new
        /// placement, or kNoWorldId when the world will not take it.
        std::function<openeden::sdk::game::WorldId(SummonFormId base, double x, double y, double z, float heading)>
            place;
        /// Takes an actor out of the world (a created one is gone for good, a placed corpse is hidden).
        std::function<void(openeden::sdk::game::WorldId reference)> remove;
        /// Makes the actor follow the player (true) or not (false).
        std::function<void(openeden::sdk::game::WorldId reference, bool follow)> follow;
    };

    /// Where a summon appears: `distance` ahead of (x, y) along `heading`, `rise` above z.
    struct SummonSpot
    {
        double x = 0.0, y = 0.0, z = 0.0;
    };
    [[nodiscard]] SummonSpot summonSpawnSpot(
        double x, double y, double z, double heading, double distance, double rise);

    /// The seconds a summon lives: the effect's duration, or kDefaultSummonSeconds when it is 0.
    [[nodiscard]] float summonSeconds(std::uint32_t duration);

    /// May a corpse of `targetLevel` be raised by an effect of `magnitude`? A magnitude that is 0, negative or not
    /// finite sets no limit; otherwise the level must not be above it.
    [[nodiscard]] bool reanimateLevelAllowed(std::uint16_t targetLevel, float magnitude);

    /// Is the corpse at (cx, cy) in front of the caster at (x, y) facing `heading`, within `reach`? The dot product of
    /// the caster's facing with the direction to the corpse must be at least `minDot`; a corpse on the same spot counts.
    [[nodiscard]] bool corpseInSights(
        double x, double y, double heading, double cx, double cy, double reach, double minDot);

    /// The ledger of what is conjured or raised, in the order it was made (ticks and ends follow that order, so a run
    /// is repeatable). Preallocated: nothing allocates after construction.
    class SummonLedger
    {
    public:
        SummonLedger() { mEntries.reserve(kMaxSummonEntries); }

        /// Adds a copy. False when the list is full or the reference is no placement or is already in it.
        bool add(const SummonEntry& entry);
        [[nodiscard]] SummonEntry* find(openeden::sdk::game::WorldId reference);
        [[nodiscard]] const SummonEntry* find(openeden::sdk::game::WorldId reference) const;
        /// The first entry whose actor handle is `handle` (valid and arrived).
        [[nodiscard]] SummonEntry* findByHandle(openeden::sdk::game::ActorHandle handle);
        /// How many entries `master` has.
        [[nodiscard]] std::size_t countOf(const openeden::sdk::game::CombatSubject& master) const;
        /// The oldest entry of `master`, null when it has none.
        [[nodiscard]] const SummonEntry* oldestOf(const openeden::sdk::game::CombatSubject& master) const;
        /// Removes one entry; false when it is not there.
        bool erase(openeden::sdk::game::WorldId reference);
        /// The references whose time is up at `now` (the combat clock), in ledger order.
        [[nodiscard]] std::vector<openeden::sdk::game::WorldId> expired(float now) const;
        [[nodiscard]] std::size_t size() const { return mEntries.size(); }
        [[nodiscard]] const std::vector<SummonEntry>& entries() const { return mEntries; }

    private:
        std::vector<SummonEntry> mEntries;
    };
}

#endif
