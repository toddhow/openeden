#ifndef OPENEDEN_GAME_SKYRIMSE_QUEST_ALIAS_H
#define OPENEDEN_GAME_SKYRIMSE_QUEST_ALIAS_H

// Quest alias filling (task 0187, milestone M-S6). When a quest starts, its aliases get their references: a specific
// reference (ALFR) keeps its reference, a unique actor (ALUA) gets the first placed reference of its base (before this
// task only the actor was recorded, so `ReferenceAlias.GetReference()` answered None), and a "find matching reference"
// alias (no fill subrecord) gets the first placed reference, in form id order, whose conditions pass. Pure over the
// indexes; no engine or Papyrus type.
//
// Rules (RECALLED-VERIFY, the Creation Kit wiki being unreachable; every one is an option or a counted choice):
//  - a candidate is a placed reference that is not deleted and is not initially disabled unless the alias allows
//    disabled (flag 0x80), and is not dead unless the alias allows dead (0x10);
//  - the conditions of the alias are evaluated with the candidate as the subject (run-on "subject");
//  - a reference already filling another alias of the same quest is not taken again unless the alias allows reuse
//    (flag 0x08);
//  - candidates: when a condition is GetIsID the references of that base (in id order), else the actors (references whose
//    base is an NPC_, in id order) up to a cap; an alias with no usable candidate source stays empty and is counted;
//  - an alias that fails stays empty; an optional alias (0x02) is not an error.
//
// Task 0211 (radiant quests) adds, all RECALLED-VERIFY and each a named rule:
//  - a LOCATION alias (ALLS) keeps its location in AliasFill::mReference (the save format is unchanged: a location alias
//    holds a form, not a placed reference). SpecificLocation (ALFL) is filled from the data; FromEvent takes the event's
//    L1 or L2; FindMatching takes the closest location whose conditions pass (the conditions run on each candidate
//    location, SkyrimConditionWorld::setSubjectLocation), "closest" meaning the grid square of the location's first
//    listed cell against the player's place, a location with no cell last, ties to the lower form id; InLocationAlias
//    (ALFA) takes a location inside the one another alias holds;
//  - a reference alias InLocationAlias (ALFA + ALRT) takes a reference the alias's location lists: a special reference of
//    the location reference type (ALRT) when the alias names one, else any of the location's special, persistent and
//    unique-NPC references; the conditions must pass and the reference must be eligible;
//  - ExternalAlias (ALEQ + ALEA) takes what that alias of the other quest holds;
//  - FindMatching references are tried closest to the player first when the player's place is known (the same cell, or
//    the same worldspace outside) and, with the "matching reference in loaded area" flag (0x20), only in loaded cells;
//    without a known place they are tried in id order, as before;
//  - CreatedObject and NearAlias are not answered (a script creates the object; a near alias needs a position
//    rule): they never block a start and are counted;
//  - `canStart` is the story manager's gate: every REQUIRED (not optional) alias of an answered kind that the data cannot
//    fill (find-matching, from-event, in-location) must fill, else the quest does not start. A dry run: it changes nothing.

#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/story.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace game_skyrimse
{
    class QuestAliasFiller
    {
    public:
        struct Options
        {
            /// Candidates evaluated for one alias when there is no GetIsID to narrow them.
            std::uint32_t mMaxCandidates = 20000;
        };
        struct Stats
        {
            std::uint64_t mFilled = 0; ///< aliases that gained a reference
            std::uint64_t mUniqueActorsResolved = 0;
            std::uint64_t mMatched = 0; ///< find-matching aliases filled by their conditions
            std::uint64_t mUnmatched = 0; ///< find-matching aliases nothing passed for
            std::uint64_t mEvaluated = 0; ///< candidate references whose conditions were evaluated
            std::uint64_t mNotAnswered = 0; ///< aliases of a kind this filler does not fill
            std::uint64_t mLocationsFilled = 0; ///< task 0211: location aliases that gained a location
            std::uint64_t mInLocationFilled = 0; ///< reference aliases filled from a location's lists
            std::uint64_t mExternalFilled = 0;
            std::uint64_t mCreatedNotAnswered = 0; ///< created-object and near aliases (never block a start)
            std::uint64_t mStartsBlocked = 0; ///< canStart said no
            std::uint64_t mPlans = 0; ///< canStart dry runs
        };

        /// Evaluates the alias's conditions with `reference` (of base `base`) as the subject. The caller owns the
        /// world, the plugin resolver of the quest and the quest context.
        using Matcher = std::function<bool(const QuestRecord& quest, const QuestAlias& alias, FormId reference, FormId base)>;

        /// Where the player stands, as the pack knows it (task 0211). `mKnown` false: unknown.
        struct PlayerSite
        {
            bool mKnown = false;
            bool mInterior = false;
            FormId mWorld = kNullFormId; ///< exterior: the worldspace
            FormId mCell = kNullFormId; ///< interior: the cell
            FormId mLocation = kNullFormId; ///< interior: its location (the world position is taken from the nearest listed cell up its chain)
            double mX = 0.0, mY = 0.0; ///< world position (units); an interior has none unless mWorld is set
        };
        /// What the pack knows about a cell of a placed reference.
        struct CellFacts
        {
            bool mKnown = false;
            bool mInterior = false;
            FormId mWorld = kNullFormId;
            bool mLoaded = false; ///< the engine has built it
        };
        /// Evaluates a location alias's conditions with `location` as the subject.
        using LocationMatcher = std::function<bool(const QuestRecord& quest, const QuestAlias& alias, FormId location)>;
        struct Environment
        {
            const LocationIndex* mLocations = nullptr;
            LocationMatcher mLocationMatcher; ///< empty: every location passes
            std::function<PlayerSite()> mPlayer; ///< empty: the player's place is unknown
            std::function<CellFacts(FormId cell)> mCell; ///< empty: nothing is known about a cell
        };
        /// The size of the grid square of an exterior cell, in units (the position of a location is the centre of the first
        /// cell it lists).
        static constexpr double kCellSize = 4096.0;

        QuestAliasFiller(const QuestIndex& quests, const PlacedRefIndex& refs, Matcher matcher, Options options = {});

        /// Fills the aliases of the quest that are not filled yet. `matching` false: only the data-fillable kinds
        /// (specific reference, unique actor). Returns the number of aliases newly filled.
        std::size_t fillQuest(FormId quest, QuestState& state, bool matching);
        /// Fills one alias (the lazy path of ReferenceAlias.GetReference). True when it holds a reference afterwards.
        bool fillAlias(FormId quest, std::uint32_t alias, QuestState& state);

        /// Task 0194: do the quest's required event aliases (ALFE/ALFD: 'R1', 'R2') have a reference in `event` whose
        /// conditions pass? A quest with no event alias always matches. Pure: fills nothing.
        [[nodiscard]] bool eventAliasesMatch(FormId quest, const StoryEvent& event);
        /// Fills the event aliases from `event` (the ones `eventAliasesMatch` accepted). Returns how many were filled.
        std::size_t fillEventAliases(FormId quest, QuestState& state, const StoryEvent& event);

        /// Task 0211: the pack's view of the world; the filler works without it as before.
        void setEnvironment(Environment environment) { mEnvironment = std::move(environment); }

        /// The story manager's gate: would every required alias of an answered kind fill for this quest now (with `event`'s
        /// members for the from-event aliases; null: an event alias has nothing to fill from)? Changes nothing.
        [[nodiscard]] bool canStart(FormId quest, const QuestState& state, const StoryEvent* event);

        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        using FillMap = std::map<std::uint32_t, AliasFill>;
        enum class Result : std::uint8_t
        {
            Filled,
            Empty,
            Waiting, ///< depends on an alias that is not filled (yet)
        };
        bool anchorOf(const PlayerSite& site, FormId& world, double& x, double& y) const;
        Result pickLocation(const QuestRecord& quest, const QuestAlias& alias, const QuestState& state,
            const FillMap& plan, FormId parent, bool dry, AliasFill& out);
        bool eventFill(const QuestRecord& quest, const QuestAlias& alias, const StoryEvent& event, AliasFill& out) const;
        Result pick(const QuestRecord& quest, const QuestAlias& alias, const QuestState& state, const FillMap& plan,
            const StoryEvent* event, bool matching, bool dry, AliasFill& out);
        void planQuest(const QuestRecord& quest, const QuestState& state, const StoryEvent* event, bool matching,
            bool dry, std::optional<std::uint32_t> only, FillMap& plan, bool& blocked);

        const QuestIndex& mQuests;
        const PlacedRefIndex& mRefs;
        Matcher mMatcher;
        Options mOptions;
        Stats mStats;
        Stats mDryStats; ///< what dry runs would have counted; never reported
        Environment mEnvironment;
    };
}

#endif
