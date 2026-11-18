#include "game_skyrimse/quest_alias.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <unordered_map>

// Rules and their status: see the header (task 0187 and task 0211).

namespace game_skyrimse
{
    namespace
    {
        constexpr std::uint16_t kGetIsId = 72;
        constexpr FormId kPlayerRefId = 0x14;
        constexpr FormId kPlayerBaseId = 0x7;
        constexpr double kFar = std::numeric_limits<double>::max();

        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }

        bool filled(const AliasFill& fill)
        {
            return fill.mKnown && (fill.mReference != kNullFormId || fill.mActor != kNullFormId);
        }

        constexpr std::uint16_t kHasSameEditorLocAsRefAlias = 181;
        constexpr std::uint16_t kGetIsEditorLocAlias = 567;

        /// Tells the environment which reference each alias holds while a location alias's conditions are evaluated.
        class AliasViewScope
        {
        public:
            AliasViewScope(const QuestAliasFiller::Environment& environment, std::function<FormId(std::uint32_t)> view)
                : mEnvironment(environment)
                , mView(std::move(view))
            {
                if (mEnvironment.mSetAliasView)
                    mEnvironment.mSetAliasView(mView);
            }
            ~AliasViewScope()
            {
                if (mEnvironment.mSetAliasView)
                    mEnvironment.mSetAliasView(std::function<FormId(std::uint32_t)>());
            }
            AliasViewScope(const AliasViewScope&) = delete;
            AliasViewScope& operator=(const AliasViewScope&) = delete;

        private:
            const QuestAliasFiller::Environment& mEnvironment;
            std::function<FormId(std::uint32_t)> mView;
        };

        /// The kinds this filler decides: a required alias of one of them that cannot fill blocks a start.
        bool blockingKind(const QuestAlias& alias)
        {
            // a find-matching REFERENCE is not one: its conditions ask for actor state the neutral world cannot all
            // answer (distance to the event's references, voice types, relationships), so a strict gate would stop
            // quests the game starts (the ambient dialogue scenes need an onlooker). Decision for toddhow (task 0211).
            if (!alias.mIsLocation && alias.mFill == AliasFillKind::FindMatching)
                return false;
            return alias.mFill == AliasFillKind::FindMatching || alias.mFill == AliasFillKind::FromEvent
                || alias.mFill == AliasFillKind::InLocationAlias;
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

    // Where the player is for the purpose of "closest": an exterior place as it is, an interior place by the position
    // of the first cell of the nearest location up its chain (the interior itself has no position in the world).
    bool QuestAliasFiller::anchorOf(const PlayerSite& site, FormId& world, double& x, double& y) const
    {
        if (!site.mKnown)
            return false;
        if (!site.mInterior || site.mWorld != kNullFormId)
        {
            world = site.mWorld;
            x = site.mX;
            y = site.mY;
            return usable(world);
        }
        const LocationIndex* locations = mEnvironment.mLocations;
        FormId at = site.mLocation;
        for (int step = 0; locations != nullptr && usable(at) && step < 64; ++step)
        {
            FormId w = kNullFormId;
            std::int32_t gx = 0, gy = 0;
            if (locations->gridOf(at, w, gx, gy))
            {
                world = w;
                x = (static_cast<double>(gx) + 0.5) * kCellSize;
                y = (static_cast<double>(gy) + 0.5) * kCellSize;
                return true;
            }
            const LocationRecord* l = locations->location(at);
            at = l != nullptr ? l->mParent : kNullFormId;
        }
        return false;
    }

    QuestAliasFiller::Result QuestAliasFiller::pickLocation(const QuestRecord& quest, const QuestAlias& alias,
        const QuestState& state, const FillMap& plan, FormId parent, bool dry, AliasFill& out)
    {
        Stats& st = dry ? mDryStats : mStats;
        const LocationIndex* locations = mEnvironment.mLocations;
        if (locations == nullptr)
            return Result::Empty;
        const FormId questId = quest.mCommon.mId;
        const PlayerSite site = mEnvironment.mPlayer ? mEnvironment.mPlayer() : PlayerSite();
        FormId anchorWorld = kNullFormId;
        double ax = 0.0, ay = 0.0;
        const bool anchored = anchorOf(site, anchorWorld, ax, ay);
        struct Candidate
        {
            double mDistance;
            FormId mLocation;
        };
        std::vector<Candidate> candidates;
        // the fill of an alias so far: this plan's, else the saved state's
        const auto fillOf = [&](std::uint32_t aliasId) {
            const auto it = plan.find(aliasId);
            return it != plan.end() ? it->second : state.aliasFill(questId, aliasId);
        };
        for (const survey::Ctda& c : alias.mConditions) // task 0226: HasSameEditorLocAsRefAlias reads another alias
            if (c.mFunction == kHasSameEditorLocAsRefAlias && !filled(fillOf(c.mParam1)))
                return Result::Waiting;
        for (const FormId id : locations->ids())
        {
            if (usable(parent) && (id == parent || !locations->isWithin(id, parent)))
                continue;
            if (usable(alias.mKeyword) && !locations->hasKeyword(id, alias.mKeyword))
                continue;
            double distance = 0.0;
            if (anchored)
            {
                FormId w = kNullFormId;
                std::int32_t gx = 0, gy = 0;
                distance = kFar;
                if (locations->gridOf(id, w, gx, gy) && w == anchorWorld)
                    distance = std::hypot((static_cast<double>(gx) + 0.5) * kCellSize - ax,
                        (static_cast<double>(gy) + 0.5) * kCellSize - ay);
            }
            candidates.push_back({ distance, id });
        }
        if (anchored)
            std::stable_sort(candidates.begin(), candidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.mDistance < b.mDistance; });
        const auto used = [&](FormId location) {
            if ((alias.mFlags & AliasFlags::AllowReuseInQuest) != 0)
                return false;
            for (const QuestAlias& other : quest.mAliases)
            {
                if (other.mId == alias.mId || !other.mIsLocation)
                    continue;
                const auto it = plan.find(other.mId);
                const AliasFill f = it != plan.end() ? it->second : state.aliasFill(questId, other.mId);
                if (f.mReference == location)
                    return true;
            }
            return false;
        };
        const AliasViewScope view(mEnvironment, [&](std::uint32_t aliasId) { return fillOf(aliasId).mReference; });
        std::uint32_t evaluated = 0;
        for (const Candidate& c : candidates)
        {
            if (evaluated >= mOptions.mMaxCandidates)
                break;
            if (used(c.mLocation))
                continue;
            ++evaluated;
            ++st.mEvaluated;
            if (mEnvironment.mLocationMatcher && !mEnvironment.mLocationMatcher(quest, alias, c.mLocation))
                continue;
            out = AliasFill();
            out.mKnown = true;
            out.mReference = c.mLocation;
            ++st.mFilled;
            ++st.mLocationsFilled;
            return Result::Filled;
        }
        ++st.mUnmatched;
        return Result::Empty;
    }

    QuestAliasFiller::Result QuestAliasFiller::pickLocationOfReference(const QuestRecord& quest,
        const QuestAlias& alias, const QuestAlias& source, const QuestState& state, const FillMap& plan, bool dry,
        AliasFill& out)
    {
        Stats& st = dry ? mDryStats : mStats;
        const LocationIndex* locations = mEnvironment.mLocations;
        if (locations == nullptr)
            return Result::Empty;
        const FormId questId = quest.mCommon.mId;
        const auto fillOf = [&](std::uint32_t aliasId) {
            const auto it = plan.find(aliasId);
            return it != plan.end() ? it->second : state.aliasFill(questId, aliasId);
        };
        const AliasFill where = fillOf(source.mId);
        if (!where.mKnown || !usable(where.mReference))
            return Result::Waiting;
        FormId at = locations->editorLocationOf(where.mReference);
        if (!usable(at) && mEnvironment.mReferenceLocation)
            at = mEnvironment.mReferenceLocation(where.mReference);
        FormId found = kNullFormId;
        if (usable(at))
        {
            if (usable(alias.mKeyword))
            {
                found = locations->ancestorWithKeyword(at, alias.mKeyword);
                if (!usable(found))
                    found = locations->descendantWithKeyword(at, alias.mKeyword);
            }
            else
            {
                found = at;
            }
        }
        if (!usable(found))
        {
            ++st.mUnmatched;
            return Result::Empty;
        }
        if ((alias.mFlags & AliasFlags::AllowReuseInQuest) == 0)
            for (const QuestAlias& other : quest.mAliases)
                if (other.mId != alias.mId && other.mIsLocation && fillOf(other.mId).mReference == found)
                {
                    ++st.mUnmatched;
                    return Result::Empty;
                }
        {
            const AliasViewScope view(mEnvironment, [&](std::uint32_t aliasId) { return fillOf(aliasId).mReference; });
            ++st.mEvaluated;
            if (mEnvironment.mLocationMatcher && !mEnvironment.mLocationMatcher(quest, alias, found))
            {
                ++st.mUnmatched;
                return Result::Empty;
            }
        }
        out = AliasFill();
        out.mKnown = true;
        out.mReference = found;
        ++st.mFilled;
        ++st.mLocationsFilled;
        return Result::Filled;
    }

    bool QuestAliasFiller::eventFill(
        const QuestRecord& quest, const QuestAlias& alias, const StoryEvent& event, AliasFill& out) const
    {
        const char kind = static_cast<char>(alias.mEventData & 0xFF);
        const char which = static_cast<char>((alias.mEventData >> 8) & 0xFF);
        if (alias.mIsLocation)
        {
            if (kind != 'L')
                return false;
            const FormId location = which == '1' ? event.mLocation1 : which == '2' ? event.mLocation2 : kNullFormId;
            if (!usable(location))
                return false;
            if (mEnvironment.mLocationMatcher && !mEnvironment.mLocationMatcher(quest, alias, location))
                return false;
            out = AliasFill();
            out.mKnown = true;
            out.mReference = location;
            return true;
        }
        if (kind != 'R')
            return false;
        const FormId ref = which == '1' ? event.mRef1 : which == '2' ? event.mRef2 : kNullFormId;
        if (!usable(ref))
            return false;
        FormId base = kNullFormId;
        if (ref == kPlayerRefId)
        {
            base = kPlayerBaseId; // the player is a reference of every world and is not in the placed table
        }
        else
        {
            const PlacedEntry* entry = mRefs.find(ref);
            if (entry != nullptr)
                base = entry->mBase;
            else if (mEnvironment.mCreatedBase)
                base = mEnvironment.mCreatedBase(ref); // task 0276
            if (base == kNullFormId)
                return false;
        }
        if (mMatcher && !mMatcher(quest, alias, ref, base))
            return false;
        out = AliasFill();
        out.mKnown = true;
        out.mReference = ref;
        return true;
    }

    QuestAliasFiller::Result QuestAliasFiller::pick(const QuestRecord& quest, const QuestAlias& alias,
        const QuestState& state, const FillMap& plan, const StoryEvent* event, bool matching, bool dry, AliasFill& out)
    {
        Stats& st = dry ? mDryStats : mStats;
        const FormId questId = quest.mCommon.mId;
        const auto currentOf = [&](std::uint32_t id) {
            const auto it = plan.find(id);
            return it != plan.end() ? it->second : state.aliasFill(questId, id);
        };
        const AliasFill current = currentOf(alias.mId);
        if (current.mKnown && current.mReference != kNullFormId)
            return Result::Empty; // filled already
        // an alias of another quest
        const auto external = [&]() {
            if (!usable(alias.mExternalQuest))
                return Result::Empty;
            const AliasFill other = state.aliasFill(alias.mExternalQuest, alias.mOtherAlias);
            if (!filled(other))
                return Result::Empty;
            out = other;
            ++st.mFilled;
            ++st.mExternalFilled;
            return Result::Filled;
        };

        if (alias.mIsLocation)
        {
            switch (alias.mFill)
            {
                case AliasFillKind::SpecificLocation:
                    if (!usable(alias.mLocation))
                        return Result::Empty;
                    out = AliasFill();
                    out.mKnown = true;
                    out.mReference = alias.mLocation;
                    ++st.mFilled;
                    ++st.mLocationsFilled;
                    return Result::Filled;
                case AliasFillKind::FromEvent:
                    if (event == nullptr || !eventFill(quest, alias, *event, out))
                        return Result::Empty;
                    ++st.mFilled;
                    ++st.mLocationsFilled;
                    return Result::Filled;
                case AliasFillKind::FindMatching:
                    return matching ? pickLocation(quest, alias, state, plan, kNullFormId, dry, out) : Result::Empty;
                case AliasFillKind::InLocationAlias:
                {
                    if (!matching)
                        return Result::Empty;
                    const QuestAlias* source = quest.alias(alias.mOtherAlias);
                    if (source != nullptr && !source->mIsLocation)
                        return pickLocationOfReference(quest, alias, *source, state, plan, dry, out);
                    const AliasFill parent = currentOf(alias.mOtherAlias);
                    if (!parent.mKnown || !usable(parent.mReference))
                        return Result::Waiting;
                    return pickLocation(quest, alias, state, plan, parent.mReference, dry, out);
                }
                case AliasFillKind::ExternalAlias:
                    return external();
                default:
                    if (matching)
                        ++st.mNotAnswered;
                    return Result::Empty;
            }
        }

        // ---- reference aliases ------------------------------------------------------------------------------------
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
                    return Result::Empty;
                const PlacedRefIndex::Range range = mRefs.byBase(alias.mActor);
                for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                {
                    if (!eligible(**it))
                        continue;
                    out = AliasFill();
                    out.mKnown = true;
                    out.mActor = alias.mActor;
                    out.mReference = (*it)->mId;
                    ++st.mFilled;
                    ++st.mUniqueActorsResolved;
                    return Result::Filled;
                }
                return Result::Empty;
            }
            case AliasFillKind::SpecificReference:
                return Result::Empty; // QuestState::initial already holds it
            case AliasFillKind::FromEvent:
                if (event == nullptr || !eventFill(quest, alias, *event, out))
                    return Result::Empty;
                ++st.mFilled;
                return Result::Filled;
            case AliasFillKind::ExternalAlias:
                return external();
            case AliasFillKind::FindMatching:
            case AliasFillKind::InLocationAlias:
                break;
            default:
                if (matching)
                {
                    ++st.mNotAnswered;
                    if (alias.mFill == AliasFillKind::CreatedObject || alias.mFill == AliasFillKind::NearAlias)
                        ++st.mCreatedNotAnswered;
                }
                return Result::Empty;
        }
        if (!matching || !mMatcher)
            return Result::Empty;
        if (dry && alias.mFill == AliasFillKind::FindMatching)
            return Result::Empty; // not a blocking kind: a dry run does not look (it would cost every actor of the
                                  // world)

        // task 0230: GetIsEditorLocAlias reads another alias (the Hold, the Location): until that one is filled the
        // conditions would fail for every candidate, so the alias waits for it (as a location alias does for fn_181)
        for (const survey::Ctda& c : alias.mConditions)
            if (c.mFunction == kGetIsEditorLocAlias && !filled(currentOf(c.mParam1)))
                return Result::Waiting;

        const auto used = [&](FormId reference) {
            if ((alias.mFlags & AliasFlags::AllowReuseInQuest) != 0)
                return false;
            for (const QuestAlias& other : quest.mAliases)
                if (other.mId != alias.mId && currentOf(other.mId).mReference == reference)
                    return true;
            return false;
        };

        // the candidates, in the order they are tried
        std::vector<const PlacedEntry*> order;
        const PlayerSite site = mEnvironment.mPlayer ? mEnvironment.mPlayer() : PlayerSite();
        FormId anchorWorld = kNullFormId;
        double ax = 0.0, ay = 0.0;
        const bool anchored = mEnvironment.mCell && anchorOf(site, anchorWorld, ax, ay);
        std::unordered_map<FormId, CellFacts> facts;
        const auto factsOf = [&](FormId cell) -> const CellFacts& {
            const auto it = facts.find(cell);
            if (it != facts.end())
                return it->second;
            return facts.emplace(cell, mEnvironment.mCell ? mEnvironment.mCell(cell) : CellFacts()).first->second;
        };
        const bool loadedOnly = (alias.mFlags & AliasFlags::MatchingRefInLoadedArea) != 0 && mEnvironment.mCell;
        const auto consider = [&](const PlacedEntry& e) {
            if (!eligible(e))
                return;
            if (loadedOnly && !factsOf(e.mCell).mLoaded)
                return;
            order.push_back(&e);
        };

        if (alias.mFill == AliasFillKind::InLocationAlias)
        {
            const AliasFill where = currentOf(alias.mOtherAlias);
            if (!where.mKnown || !usable(where.mReference))
                return Result::Waiting;
            const LocationIndex* locations = mEnvironment.mLocations;
            const LocationRecord* record = locations != nullptr ? locations->location(where.mReference) : nullptr;
            if (record == nullptr)
                return Result::Empty;
            std::vector<FormId> listed;
            if (usable(alias.mReferenceType))
            {
                for (const LocationSpecialRef& r : locations->specialRefsOfType(where.mReference, alias.mReferenceType))
                    listed.push_back(r.mRef);
            }
            else
            {
                for (const LocationSpecialRef& r : record->mSpecialRefs)
                    listed.push_back(r.mRef);
                for (const LocationPersistentRef& r : record->mPersistentRefs)
                    listed.push_back(r.mRef);
                for (const LocationUniqueNpc& n : record->mUniqueNpcs)
                    listed.push_back(n.mRef);
            }
            std::set<FormId> seen;
            std::size_t found = 0;
            for (const FormId ref : listed)
            {
                if (!usable(ref) || !seen.insert(ref).second)
                    continue;
                if (const PlacedEntry* entry = mRefs.find(ref))
                {
                    ++found;
                    consider(*entry);
                }
            }
            if (mTrace) // task 0305: why a reference in a location is not filled
            {
                char text[160];
                std::snprintf(text, sizeof(text), "  alias %s in location %08X: %zu listed, %zu placed, %zu eligible",
                    alias.mName.c_str(), static_cast<unsigned>(where.mReference), listed.size(), found, order.size());
                mTrace(text);
            }
        }
        else
        {
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
            if (usable(narrowed))
            {
                const PlacedRefIndex::Range range = mRefs.byBase(narrowed);
                for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                    consider(**it);
            }
            else
            {
                for (const PlacedEntry& e : mRefs.all())
                    if ((e.mFlags & PlacedFlags::IsActor) != 0)
                        consider(e);
            }
        }

        if (anchored)
        {
            // closest first: the same interior cell counts as here, another interior as far away, an exterior by its
            // distance when it is in the anchor's world
            std::vector<std::pair<double, const PlacedEntry*>> keyed;
            keyed.reserve(order.size());
            for (const PlacedEntry* e : order)
            {
                const CellFacts& f = factsOf(e->mCell);
                double distance = kFar;
                if (f.mKnown)
                {
                    if (site.mInterior && e->mCell == site.mCell)
                        distance = 0.0;
                    else if (!f.mInterior && f.mWorld == anchorWorld)
                        distance = std::hypot(static_cast<double>(e->mX) - ax, static_cast<double>(e->mY) - ay);
                }
                keyed.emplace_back(distance, e);
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                [](const std::pair<double, const PlacedEntry*>& a, const std::pair<double, const PlacedEntry*>& b) {
                    return a.first < b.first;
                });
            for (std::size_t i = 0; i < keyed.size(); ++i)
                order[i] = keyed[i].second;
        }

        // task 0230: the conditions read the aliases filled so far (GetIsEditorLocAlias names the Hold or Location
        // alias)
        const AliasViewScope view(mEnvironment, [&](std::uint32_t aliasId) { return currentOf(aliasId).mReference; });
        std::uint32_t evaluated = 0;
        for (const PlacedEntry* e : order)
        {
            if (evaluated >= mOptions.mMaxCandidates)
                break;
            if (used(e->mId))
                continue;
            ++evaluated;
            ++st.mEvaluated;
            if (!mMatcher(quest, alias, e->mId, e->mBase))
                continue;
            out = AliasFill();
            out.mKnown = true;
            out.mReference = e->mId;
            ++st.mFilled;
            if (alias.mFill == AliasFillKind::InLocationAlias)
                ++st.mInLocationFilled;
            else
                ++st.mMatched;
            return Result::Filled;
        }
        ++st.mUnmatched;
        return Result::Empty;
    }

    void QuestAliasFiller::planQuest(const QuestRecord& quest, const QuestState& state, const StoryEvent* event,
        bool matching, bool dry, std::optional<std::uint32_t> only, FillMap& plan, bool& blocked)
    {
        const FormId questId = quest.mCommon.mId;
        const QuestAlias* target = only ? quest.alias(*only) : nullptr;
        const auto wanted = [&](const QuestAlias& a) {
            if (!only)
                return true;
            if (a.mId == *only)
                return true;
            return target != nullptr && target->mFill == AliasFillKind::InLocationAlias && a.mId == target->mOtherAlias;
        };
        // locations first (a reference alias may live in one), then references; an alias that waits for another one is
        // asked again in the next round
        std::vector<const QuestAlias*> pending;
        for (const bool locations : { true, false })
            for (const QuestAlias& a : quest.mAliases)
                if (a.mIsLocation == locations && wanted(a))
                    pending.push_back(&a);
        for (int round = 0; round < 3 && !pending.empty(); ++round)
        {
            std::vector<const QuestAlias*> waiting;
            for (const QuestAlias* a : pending)
            {
                AliasFill fill;
                const Result r = pick(quest, *a, state, plan, event, matching, dry, fill);
                if (r == Result::Filled)
                    plan[a->mId] = fill;
                else if (r == Result::Waiting)
                    waiting.push_back(a);
            }
            if (waiting.size() == pending.size())
                break;
            pending = std::move(waiting);
        }
        blocked = false;
        for (const QuestAlias& a : quest.mAliases)
        {
            if ((a.mFlags & AliasFlags::Optional) != 0 || !blockingKind(a) || !wanted(a))
                continue;
            const auto it = plan.find(a.mId);
            const AliasFill f = it != plan.end() ? it->second : state.aliasFill(questId, a.mId);
            if (!filled(f))
            {
                blocked = true;
                if (mTrace && dry)
                    mTrace("  alias " + a.mName + " (" + (a.mIsLocation ? "location" : "reference") + " fill "
                        + std::to_string(static_cast<int>(a.mFill)) + ") of " + quest.mCommon.mEditorId
                        + " is not filled");
            }
        }
    }

    std::size_t QuestAliasFiller::fillQuest(FormId quest, QuestState& state, bool matching)
    {
        const QuestRecord* record = mQuests.quest(quest);
        if (record == nullptr)
            return 0;
        FillMap plan;
        bool blocked = false;
        planQuest(*record, state, nullptr, matching, false, std::nullopt, plan, blocked);
        for (const auto& [id, fill] : plan)
            state.setAliasFill(quest, id, fill);
        return plan.size();
    }

    // Task 0230. Only an alias whose conditions call GetIsEditorLocAlias (fn_567) is tried: that function was
    // unanswered, so the aliases that need it were not filled at the start of the quest; every other empty alias keeps
    // the rule it had.
    std::size_t QuestAliasFiller::fillForSpeaker(QuestState& state, FormId reference, FormId base)
    {
        if (!usable(reference) || !mMatcher)
            return 0;
        const PlacedEntry* entry = mRefs.find(reference);
        if (entry == nullptr || entry->mDeleted)
            return 0;
        const FormId entryBase = usable(entry->mBase) ? entry->mBase : base;
        std::size_t count = 0;
        for (const FormId questId : mQuests.questIds())
        {
            if (!state.running(questId))
                continue;
            const QuestRecord* quest = mQuests.quest(questId);
            if (quest == nullptr)
                continue;
            for (const QuestAlias& alias : quest->mAliases)
            {
                if (alias.mIsLocation || alias.mFill != AliasFillKind::FindMatching)
                    continue;
                if (std::none_of(alias.mConditions.begin(), alias.mConditions.end(),
                        [](const survey::Ctda& c) { return c.mFunction == kGetIsEditorLocAlias; }))
                    continue;
                if (filled(state.aliasFill(questId, alias.mId)))
                    continue;
                if ((alias.mFlags & AliasFlags::AllowDisabled) == 0 && mRefs.initiallyDisabled(*entry))
                    continue;
                if ((alias.mFlags & AliasFlags::AllowDead) == 0 && (entry->mFlags & PlacedFlags::Dead) != 0)
                    continue;
                if ((alias.mFlags & AliasFlags::AllowReuseInQuest) == 0
                    && std::any_of(quest->mAliases.begin(), quest->mAliases.end(), [&](const QuestAlias& other) {
                           return other.mId != alias.mId && state.aliasFill(questId, other.mId).mReference == reference;
                       }))
                    continue;
                const AliasViewScope view(
                    mEnvironment, [&](std::uint32_t aliasId) { return state.aliasFill(questId, aliasId).mReference; });
                ++mStats.mEvaluated;
                if (!mMatcher(*quest, alias, reference, entryBase))
                    continue;
                AliasFill fill;
                fill.mKnown = true;
                fill.mReference = reference;
                state.setAliasFill(questId, alias.mId, fill);
                ++mStats.mFilled;
                ++mStats.mMatched;
                ++count;
            }
        }
        return count;
    }

    bool QuestAliasFiller::fillAlias(FormId quest, std::uint32_t aliasId, QuestState& state)
    {
        const QuestRecord* record = mQuests.quest(quest);
        const QuestAlias* alias = record != nullptr ? record->alias(aliasId) : nullptr;
        if (alias == nullptr)
            return false;
        FillMap plan;
        bool blocked = false;
        planQuest(*record, state, nullptr, true, false, aliasId, plan, blocked);
        for (const auto& [id, fill] : plan)
            state.setAliasFill(quest, id, fill);
        const AliasFill fill = state.aliasFill(quest, aliasId);
        return fill.mKnown && fill.mReference != kNullFormId;
    }

    bool QuestAliasFiller::canStart(FormId questId, const QuestState& state, const StoryEvent* event)
    {
        const QuestRecord* quest = mQuests.quest(questId);
        if (quest == nullptr)
            return false;
        const bool any = std::any_of(quest->mAliases.begin(), quest->mAliases.end(),
            [](const QuestAlias& a) { return (a.mFlags & AliasFlags::Optional) == 0 && blockingKind(a); });
        if (!any)
            return true;
        ++mStats.mPlans;
        FillMap plan;
        bool blocked = false;
        planQuest(*quest, state, event, true, true, std::nullopt, plan, blocked);
        if (blocked)
            ++mStats.mStartsBlocked;
        return !blocked;
    }

    bool QuestAliasFiller::eventAliasesMatch(FormId questId, const StoryEvent& event)
    {
        const QuestRecord* quest = mQuests.quest(questId);
        if (quest == nullptr)
            return false;
        for (const QuestAlias& alias : quest->mAliases)
        {
            if (alias.mFill != AliasFillKind::FromEvent)
                continue;
            AliasFill ignored;
            if (!eventFill(*quest, alias, event, ignored) && (alias.mFlags & AliasFlags::Optional) == 0)
                return false;
        }
        return true;
    }

    std::size_t QuestAliasFiller::fillEventAliases(FormId questId, QuestState& state, const StoryEvent& event)
    {
        const QuestRecord* quest = mQuests.quest(questId);
        if (quest == nullptr)
            return 0;
        std::size_t count = 0;
        for (const QuestAlias& alias : quest->mAliases)
        {
            if (alias.mFill != AliasFillKind::FromEvent)
                continue;
            AliasFill fill;
            if (!eventFill(*quest, alias, event, fill))
                continue;
            state.setAliasFill(questId, alias.mId, fill);
            ++mStats.mFilled;
            if (alias.mIsLocation)
                ++mStats.mLocationsFilled;
            ++count;
        }
        return count;
    }
}
