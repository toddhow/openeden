#ifndef OPENEDEN_GAME_SKYRIMSE_MAGIC_CONDITIONS_H
#define OPENEDEN_GAME_SKYRIMSE_MAGIC_CONDITIONS_H

// Task 0236: the conditions on the effects of spells, poisons and enchantments (the CTDA subrecords after an effect's
// EFID/EFIT, and the MGEF's own). The magic runtime of task 0186 skipped every effect that has any; this evaluates
// them through the conditions engine of task 0122 over two actors, the effect's target and its caster.
//
// What the data shows (tools/skyrimse_effect_conditions_survey, the five masters, aggregates in the task doc):
//  - the run-on value is 0 (Subject) for 2,360 of the 2,691 conditions, 1 (Target) for 96 and 2 (Reference) for 235;
//  - Subject is the TARGET of the effect and run-on 1 the CASTER: AlchParalysis asks HasKeyword(ImmuneParalysis) == 0
//    and GetRestrained == 0 of the actor it paralyses, PerkImpactStaggerPushFFAimed asks HasPerk(Impact) and
//    EffectWasDualCast of run-on 1, and Impact is the caster's perk (VERIFIED by those records);
//  - run-on 2 with the reference 0x14 is the player (the form id of the player reference is 0x14 in every plugin).
// The names of the functions are xEdit's table (survey_names_xedit.cpp); the meanings below are the CK wiki's and
// CommonLibSSE-NG's (decision 0014's amendment) and RECALLED-VERIFY unless the data settles them.
//
// Answered (the world has the facts in combat): 560 HasKeyword, 448 HasPerk, 715 IsUndead (the actor has the keyword
// ActorTypeUndead, form 0x13796, VERIFIED to exist in Skyrim.esm; whether the game's own test is the same keyword is
// RECALLED-VERIFY), 72 GetIsID, 71 GetInFaction, 69 GetIsRace, 70 GetIsSex, 80 GetLevel, 14 GetActorValue, 277
// GetBaseActorValue, 640 GetActorValuePercent (health, magicka and stamina; any other value reads as full), 597
// GetEquippedItemType, 682 WornHasKeyword, 699 HasMagicEffectKeyword, 214 HasMagicEffect (an effect running on the
// actor now), 289 IsInCombat, 46 GetDead, 77 GetRandomPercent (seeded: the same cast gives the same number), 244
// GetRestrained (always 0: nothing here restrains an actor), 700 IsCommandedActor (always 0: nothing commands one) and
// 724 EffectWasDualCast (always 0: there is no dual casting).
// Everything else is UNANSWERED: the list fails closed, that is the effect does nothing, and the function is logged
// once.
//
// Task 0238 adds six more, for an evaluator that was told it may (`setExtended`, which the combat does) and, for three
// of them, given the source they read: 74 GetGlobalValue (a globals source, `setGlobalLookup`; a global the source does
// not know leaves the list unanswered), 300 IsInInterior and 310 GetInWorldspace (the place, `setPlace`: the effect's
// target stands where the player does, because only the loaded cells have actors), 365 IsChild (the race's Child flag,
// RACE DATA +32 bit 2, VERIFIED: every *RaceChild race has it and no adult race does), 719 IsHostileToActor (the
// participant's hostility to the caster or to the player, from the combat's relation rules; a parameter other than none
// or the player reference is unanswered) and 722 WornApparelHasKeywordCount (the number of worn pieces that carry the
// keyword). Not answered: 629 GetVMQuestVariable (a quest script's variable; needs the Papyrus host), the quest
// functions.

#include <game_skyrimse/perks.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <span>
#include <string>

namespace game_skyrimse::effectconditions
{
    /// ActorTypeUndead (KYWD of Skyrim.esm, VERIFIED).
    inline constexpr FormId kKeywordUndead = 0x00013796;
    /// The player reference's form id (a CTDA's reference field with run-on 2).
    inline constexpr std::uint32_t kPlayerReference = 0x14;
    /// Subject handles the world gives out.
    inline constexpr conditions::Subject kSubjectTarget = 1;
    inline constexpr conditions::Subject kSubjectCaster = 2;
    inline constexpr conditions::Subject kSubjectPlayer = 3;

    /// One actor a condition can ask about. Pointers and spans are not owned and must outlive the evaluation.
    struct Participant
    {
        PerkParticipant mPerk; ///< base, race, level, keywords, perks, factions, worn keywords, values, hand type
        bool mDead = false;
        bool mInCombat = false;
        float mHealthFraction = 1.f; ///< 0 to 1
        float mMagickaFraction = 1.f;
        float mStaminaFraction = 1.f;
        std::span<const FormId> mActiveEffects{}; ///< the MGEFs running on it now
        // Task 0238 (all default to "no"): the race has the Child flag; the actor is an enemy of the caster of the
        // effect and of the player (the combat's relation rules).
        bool mChild = false;
        bool mCommanded = false; ///< task 0248: a Command Animal effect holds it (fn 700)
        bool mHostileToCaster = false;
        bool mHostileToPlayer = false;
    };

    /// Task 0238: where the effects land, for IsInInterior and GetInWorldspace. Unknown: those two stay unanswered.
    struct Place
    {
        bool mKnown = false;
        bool mInterior = false;
        FormId mWorld = kNullFormId; ///< the exterior's worldspace (the load-order id); 0 in an interior
    };

    /// True when the world can answer function `function` with nothing given (the first list above: the 21 functions of
    /// task 0236).
    [[nodiscard]] bool functionAnswered(std::uint16_t function);
    /// Task 0238: the same plus the six functions an extended evaluator can answer with its sources (74, 300, 310, 365,
    /// 719, 722).
    [[nodiscard]] bool functionAnsweredExtended(std::uint16_t function);

    /// True when a condition's run-on can be resolved: 0, 1, or 2 with the player's reference.
    [[nodiscard]] bool runOnAnswered(const survey::Ctda& condition);

    /// What a list came to.
    enum class Verdict : std::uint8_t
    {
        Pass, ///< every condition held (an empty list passes)
        Fail, ///< a condition did not hold
        Unanswered, ///< a function or run-on the world cannot answer: fails closed
    };

    /// Evaluates effect conditions. Not thread safe (it owns an evaluator); main thread.
    class Evaluator
    {
    public:
        using EffectLookup = std::function<const MagicEffectRecord*(FormId)>;
        using Log = std::function<void(const std::string&)>;
        /// Task 0238: the value of a GLOB by form id; false when the source does not know it.
        using GlobalLookup = std::function<bool(FormId global, float& value)>;

        Evaluator();

        /// Task 0238: allow the six extra functions. Off (the default) the evaluator is the one of task 0236.
        void setExtended(bool extended) { mExtended = extended; }
        void setPlace(const Place& place) { mPlace = place; }
        void setGlobalLookup(GlobalLookup lookup) { mGlobals = std::move(lookup); }

        /// Needed for HasMagicEffectKeyword (a running effect's keywords). Without it that function finds nothing.
        void setEffectLookup(EffectLookup lookup) { mLookup = std::move(lookup); }
        /// Called once per unanswered function index with one line.
        void setLog(Log log) { mLog = std::move(log); }
        /// The seed of GetRandomPercent for the next evaluations (the numbers follow splitmix64 from it).
        void setSeed(std::uint64_t seed) { mSeed = seed; }

        /// `target` is the actor the effect lands on (Subject), `caster` its caster (run-on 1), `player` the player
        /// (run-on 2 with reference 0x14); any may be null, and a condition that needs a missing one is Unanswered.
        [[nodiscard]] Verdict evaluate(std::span<const survey::Ctda> list, const Participant* target,
            const Participant* caster, const Participant* player);

        std::uint64_t mLists = 0;
        std::uint64_t mPassed = 0;
        std::uint64_t mFailed = 0;
        std::uint64_t mUnanswered = 0;
        std::set<std::uint16_t> mUnansweredFunctions; ///< the indices that made a list unanswered

    private:
        [[nodiscard]] bool answers(const survey::Ctda& condition) const;
        conditions::Evaluator mEvaluator;
        bool mExtended = false;
        Place mPlace;
        GlobalLookup mGlobals;
        EffectLookup mLookup;
        Log mLog;
        std::uint64_t mSeed = 1;
        std::uint64_t mCounter = 0;
        std::uint32_t mLoggedRunOn = 0; ///< bit n: run-on n was logged as having nobody
    };
}

#endif
