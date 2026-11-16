#include "game_skyrimse/quest_alias.hpp"

#include <algorithm>

// Rules and their status: see the header (task 0187).

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint16_t kGetIsId = 72;

        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }
    }

    QuestAliasFiller::QuestAliasFiller(
        const QuestIndex& quests, const PlacedRefIndex& refs, Matcher matcher, Options options)
        : mQuests(quests)
        , mRefs(refs)
        , mMatcher(std::move(matcher))
        , mOptions(options)
    {
    }

    bool QuestAliasFiller::fillOne(const QuestRecord& quest, const QuestAlias& alias, QuestState& state, bool matching)
    {
        const FormId questId = quest.mCommon.mId;
        const AliasFill current = state.aliasFill(questId, alias.mId);
        if (current.mKnown && current.mReference != kNullFormId)
            return false;
        // is a candidate allowed by the alias's flags?
        const auto eligible = [&](const PlacedEntry& e) {
            if (e.mDeleted)
                return false;
            if ((alias.mFlags & AliasFlags::AllowDisabled) == 0 && mRefs.initiallyDisabled(e))
                return false;
            if ((alias.mFlags & AliasFlags::AllowDead) == 0 && (e.mFlags & PlacedFlags::Dead) != 0)
                return false;
            return true;
        };
        switch (alias.mFill)
        {
            case AliasFillKind::UniqueActor:
            {
                if (!usable(alias.mActor))
                    return false;
                const PlacedRefIndex::Range range = mRefs.byBase(alias.mActor);
                for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                {
                    if (!eligible(**it))
                        continue;
                    AliasFill fill;
                    fill.mKnown = true;
                    fill.mActor = alias.mActor;
                    fill.mReference = (*it)->mId;
                    state.setAliasFill(questId, alias.mId, fill);
                    ++mStats.mFilled;
                    ++mStats.mUniqueActorsResolved;
                    return true;
                }
                return false;
            }
            case AliasFillKind::SpecificReference:
                return false; // QuestState::initial already holds it
            case AliasFillKind::FindMatching:
                break;
            default:
                if (matching)
                    ++mStats.mNotAnswered;
                return false;
        }
        if (!matching || !mMatcher)
            return false;
        // candidates: the references of a base named by a GetIsID condition, else the actors
        FormId narrowed = kNullFormId;
        for (const survey::Ctda& c : alias.mConditions)
            if (c.mFunction == kGetIsId && c.mRunOn == 0)
            {
                const std::optional<FormId> id = mQuests.resolverOf(quest).resolve(c.mParam1);
                if (id && usable(*id))
                {
                    narrowed = *id;
                    break;
                }
            }
        const auto used = [&](FormId reference) {
            if ((alias.mFlags & AliasFlags::AllowReuseInQuest) != 0)
                return false;
            for (const QuestAlias& other : quest.mAliases)
                if (other.mId != alias.mId && state.aliasFill(questId, other.mId).mReference == reference)
                    return true;
            return false;
        };
        std::uint32_t evaluated = 0;
        const auto tryCandidate = [&](const PlacedEntry& e) {
            if (!eligible(e) || used(e.mId))
                return false;
            ++evaluated;
            ++mStats.mEvaluated;
            if (!mMatcher(quest, alias, e.mId, e.mBase))
                return false;
            AliasFill fill;
            fill.mKnown = true;
            fill.mReference = e.mId;
            state.setAliasFill(questId, alias.mId, fill);
            ++mStats.mFilled;
            ++mStats.mMatched;
            return true;
        };
        if (usable(narrowed))
        {
            const PlacedRefIndex::Range range = mRefs.byBase(narrowed);
            for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd && evaluated < mOptions.mMaxCandidates;
                 ++it)
                if (tryCandidate(**it))
                    return true;
        }
        else
        {
            for (const PlacedEntry& e : mRefs.all())
            {
                if (evaluated >= mOptions.mMaxCandidates)
                    break;
                if ((e.mFlags & PlacedFlags::IsActor) != 0 && tryCandidate(e))
                    return true;
            }
        }
        ++mStats.mUnmatched;
        return false;
    }

    std::size_t QuestAliasFiller::fillQuest(FormId quest, QuestState& state, bool matching)
    {
        const QuestRecord* record = mQuests.quest(quest);
        if (record == nullptr)
            return 0;
        std::size_t filled = 0;
        for (const QuestAlias& alias : record->mAliases)
            filled += fillOne(*record, alias, state, matching) ? 1 : 0;
        return filled;
    }

    bool QuestAliasFiller::fillAlias(FormId quest, std::uint32_t aliasId, QuestState& state)
    {
        const QuestRecord* record = mQuests.quest(quest);
        const QuestAlias* alias = record != nullptr ? record->alias(aliasId) : nullptr;
        if (alias == nullptr)
            return false;
        (void)fillOne(*record, *alias, state, true);
        const AliasFill fill = state.aliasFill(quest, aliasId);
        return fill.mKnown && fill.mReference != kNullFormId;
    }
}
