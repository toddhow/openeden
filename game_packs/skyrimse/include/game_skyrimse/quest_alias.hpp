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

#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/story.hpp>

#include <cstdint>
#include <functional>

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
        };

        /// Evaluates the alias's conditions with `reference` (of base `base`) as the subject. The caller owns the
        /// world, the plugin resolver of the quest and the quest context.
        using Matcher = std::function<bool(const QuestRecord& quest, const QuestAlias& alias, FormId reference, FormId base)>;

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

        [[nodiscard]] const Stats& stats() const { return mStats; }

    private:
        bool fillOne(const QuestRecord& quest, const QuestAlias& alias, QuestState& state, bool matching);

        const QuestIndex& mQuests;
        const PlacedRefIndex& mRefs;
        Matcher mMatcher;
        Options mOptions;
        Stats mStats;
    };
}

#endif
