#include "game_skyrimse/magic_conditions.hpp"

#include <algorithm>
#include <array>

namespace game_skyrimse::effectconditions
{
    namespace
    {
        using conditions::Subject;

        /// The functions the world answers (see the header). Sorted, so the lookup is a binary search.
        constexpr std::array<std::uint16_t, 21> kAnswered = { 14, 46, 69, 70, 71, 72, 77, 80, 214, 244, 277, 289, 448,
            560, 597, 640, 682, 699, 700, 715, 724 };

        /// Task 0238: what an extended evaluator adds (sorted).
        constexpr std::array<std::uint16_t, 6> kExtendedOnly = { 74, 300, 310, 365, 719, 722 };

        /// Bit 2 of a CTDA's flags: the comparison value is a global (conditions.cpp has its own copy).
        constexpr std::uint8_t kUseGlobalFlag = 0x04;

        /// Health, magicka and stamina: the actor values 24 to 26 (VERIFIED: the Intense Flames effect asks 24).
        constexpr std::uint32_t kHealthValue = 24, kMagickaValue = 25, kStaminaValue = 26;

        std::uint64_t splitmix64(std::uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        template <class T>
        bool contains(const std::vector<T>* list, T value)
        {
            return list != nullptr && std::find(list->begin(), list->end(), value) != list->end();
        }

        float valueOf(const Participant* p, std::uint32_t index)
        {
            if (p == nullptr || p->mPerk.mValues == nullptr)
                return 0.f;
            for (const auto& [i, v] : *p->mPerk.mValues)
                if (i == index)
                    return v;
            return 0.f;
        }

        class World final : public conditions::ConditionWorld
        {
        public:
            const Participant* mTarget = nullptr;
            const Participant* mCaster = nullptr;
            const Participant* mPlayer = nullptr;
            const Evaluator::EffectLookup* mLookup = nullptr;
            std::uint64_t mSeed = 1;
            std::uint64_t* mCounter = nullptr;
            const Place* mPlace = nullptr;
            const Evaluator::GlobalLookup* mGlobals = nullptr;

            [[nodiscard]] const Participant* of(Subject subject) const
            {
                switch (subject)
                {
                    case kSubjectTarget:
                        return mTarget;
                    case kSubjectCaster:
                        return mCaster;
                    case kSubjectPlayer:
                        return mPlayer;
                    default:
                        return nullptr;
                }
            }

            Subject resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const override
            {
                if (runOn == 0)
                    return mTarget != nullptr ? kSubjectTarget : 0;
                if (runOn == 1)
                    return mCaster != nullptr ? kSubjectCaster : 0;
                if (runOn == 2 && reference == kPlayerReference)
                    return mPlayer != nullptr ? kSubjectPlayer : 0;
                return 0;
            }
            bool hasBaseId(Subject subject, std::uint32_t formId) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && p->mPerk.mForm != kNullFormId && p->mPerk.mForm == formId;
            }
            float questStage(std::uint32_t) const override { return 0.f; }
            bool questStageDone(std::uint32_t, std::uint32_t) const override { return false; }
            bool inFaction(Subject subject, std::uint32_t faction) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && contains(p->mPerk.mFactions, static_cast<FormId>(faction));
            }
            float globalValue(std::uint32_t global) const override
            {
                float value = 0.f;
                if (mGlobals != nullptr && *mGlobals && (*mGlobals)(static_cast<FormId>(global), value))
                    return value;
                return 0.f; // evaluate() has already refused a list that reads a global the source does not know
            }
            bool inInterior(Subject subject) const override
            {
                return subject != 0 && mPlace != nullptr && mPlace->mKnown && mPlace->mInterior;
            }
            bool inWorldspace(Subject subject, std::uint32_t worldspace) const override
            {
                // exteriors only: in an interior the answer is false (CK wiki), the same as the dialogue world's
                return subject != 0 && mPlace != nullptr && mPlace->mKnown && !mPlace->mInterior && worldspace != 0
                    && mPlace->mWorld == static_cast<FormId>(worldspace);
            }
            bool isChild(Subject subject) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && p->mChild;
            }
            bool hasPerk(Subject subject, std::uint32_t perk) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && contains(p->mPerk.mPerks, static_cast<FormId>(perk));
            }
            bool hasKeyword(Subject subject, std::uint32_t keyword) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && contains(p->mPerk.mKeywords, static_cast<FormId>(keyword));
            }
            bool wornHasKeyword(Subject subject, std::uint32_t keyword) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && contains(p->mPerk.mWornKeywords, static_cast<FormId>(keyword));
            }
            float baseActorValue(Subject subject, std::uint32_t index) const override
            {
                return valueOf(of(subject), index);
            }
            float actorValue(Subject subject, std::uint32_t index) const override { return valueOf(of(subject), index); }
            float actorValuePercent(Subject subject, std::uint32_t index) const override
            {
                const Participant* p = of(subject);
                if (p == nullptr)
                    return 0.f;
                if (index == kHealthValue)
                    return p->mHealthFraction;
                if (index == kMagickaValue)
                    return p->mMagickaFraction;
                if (index == kStaminaValue)
                    return p->mStaminaFraction;
                return 1.f; // every other value is at its maximum: nothing here damages one
            }
            float levelOf(Subject subject) const override
            {
                const Participant* p = of(subject);
                return p != nullptr ? static_cast<float>(p->mPerk.mLevel) : 0.f;
            }
            bool isRace(Subject subject, std::uint32_t race) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && race != 0 && p->mPerk.mRace == race;
            }
            int sexOf(Subject subject) const override
            {
                const Participant* p = of(subject);
                return p != nullptr ? (p->mPerk.mFemale ? 1 : 0) : -1;
            }
            float equippedItemType(Subject subject, std::uint32_t hand) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && hand == 1 ? static_cast<float>(p->mPerk.mRightHandType) : 0.f;
            }
            bool isDead(Subject subject) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && p->mDead;
            }
            bool inCombat(Subject subject, bool) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && p->mInCombat;
            }
            bool isRestrained(Subject) const override { return false; } // nothing here restrains an actor
            bool isCommandedActor(Subject subject) const override
            {
                const Participant* p = of(subject);
                return p != nullptr && p->mCommanded; // task 0248
            }
            bool hasMagicEffect(Subject subject, std::uint32_t effect) const override
            {
                const Participant* p = of(subject);
                if (p == nullptr || effect == 0)
                    return false;
                return std::find(p->mActiveEffects.begin(), p->mActiveEffects.end(), static_cast<FormId>(effect))
                    != p->mActiveEffects.end();
            }
            bool hasMagicEffectKeyword(Subject subject, std::uint32_t keyword) const override
            {
                const Participant* p = of(subject);
                if (p == nullptr || mLookup == nullptr || !*mLookup || keyword == 0)
                    return false;
                for (const FormId id : p->mActiveEffects)
                {
                    const MagicEffectRecord* record = (*mLookup)(id);
                    if (record != nullptr
                        && std::find(record->mKeywords.begin(), record->mKeywords.end(), static_cast<FormId>(keyword))
                            != record->mKeywords.end())
                        return true;
                }
                return false;
            }
            float randomPercent() const override
            {
                const std::uint64_t n = mCounter != nullptr ? (*mCounter)++ : 0;
                return static_cast<float>(splitmix64(mSeed + n) % 100);
            }
        };

        const Participant* participantOf(const conditions::ConditionWorld& world, Subject subject)
        {
            const auto* mine = dynamic_cast<const World*>(&world);
            return mine != nullptr ? mine->of(subject) : nullptr;
        }

        // 715 IsUndead: the subject has the keyword ActorTypeUndead (RECALLED-VERIFY that this is the game's test).
        float isUndeadFn(const survey::Ctda&, Subject subject, const conditions::ConditionWorld& world)
        {
            const Participant* p = participantOf(world, subject);
            return p != nullptr && contains(p->mPerk.mKeywords, kKeywordUndead) ? 1.f : 0.f;
        }

        // 724 EffectWasDualCast: there is no dual casting, so never.
        float wasDualCastFn(const survey::Ctda&, Subject, const conditions::ConditionWorld&)
        {
            return 0.f;
        }

        // 719 IsHostileToActor(actor): the subject is an enemy of the actor: none (parameter 0) is the effect's caster, the
        // player's reference the player (xEdit: the parameter is an actor reference; the data's two shapes are those,
        // RECALLED-VERIFY that "none" means the caster).
        float isHostileToActorFn(const survey::Ctda& c, Subject subject, const conditions::ConditionWorld& world)
        {
            const Participant* p = participantOf(world, subject);
            if (p == nullptr)
                return 0.f;
            if (c.mParam1 == kPlayerReference)
                return p->mHostileToPlayer ? 1.f : 0.f;
            return p->mHostileToCaster ? 1.f : 0.f;
        }

        // 722 WornApparelHasKeywordCount(keyword): the number of worn pieces with the keyword (the keywords of each worn
        // piece are listed once, one list per piece, so the count is the number of times the keyword appears).
        float wornKeywordCountFn(const survey::Ctda& c, Subject subject, const conditions::ConditionWorld& world)
        {
            const Participant* p = participantOf(world, subject);
            if (p == nullptr || p->mPerk.mWornKeywords == nullptr || c.mParam1 == 0)
                return 0.f;
            return static_cast<float>(std::count(p->mPerk.mWornKeywords->begin(), p->mPerk.mWornKeywords->end(),
                static_cast<FormId>(c.mParam1)));
        }

        constexpr std::array<conditions::ExtraFunction, 4> kExtra = { {
            { 715, isUndeadFn },
            { 719, isHostileToActorFn },
            { 722, wornKeywordCountFn },
            { 724, wasDualCastFn },
        } };

        /// 0 target, 1 caster, 2 player: the participant a condition's run-on needs.
        const Participant* neededFor(const survey::Ctda& c, const Participant* target, const Participant* caster,
            const Participant* player)
        {
            if (c.mRunOn == 0)
                return target;
            if (c.mRunOn == 1)
                return caster;
            return player;
        }
    }

    bool functionAnswered(std::uint16_t function)
    {
        return std::binary_search(kAnswered.begin(), kAnswered.end(), function);
    }

    bool functionAnsweredExtended(std::uint16_t function)
    {
        return functionAnswered(function) || std::binary_search(kExtendedOnly.begin(), kExtendedOnly.end(), function);
    }

    bool Evaluator::answers(const survey::Ctda& c) const
    {
        if (functionAnswered(c.mFunction))
            return true;
        if (!mExtended || !functionAnsweredExtended(c.mFunction))
            return false;
        switch (c.mFunction)
        {
            case 74: // a global the source knows
            {
                float unused = 0.f;
                return mGlobals && mGlobals(static_cast<FormId>(c.mParam1), unused);
            }
            case 300:
            case 310:
                return mPlace.mKnown;
            case 719: // the actor is none or the player
                return c.mParam1 == 0 || c.mParam1 == kPlayerReference;
            default:
                return true;
        }
    }

    bool runOnAnswered(const survey::Ctda& condition)
    {
        return condition.mRunOn == 0 || condition.mRunOn == 1
            || (condition.mRunOn == 2 && condition.mReference == kPlayerReference);
    }

    Evaluator::Evaluator()
    {
        mEvaluator.setExtraFunctions(kExtra);
    }

    Verdict Evaluator::evaluate(std::span<const survey::Ctda> list, const Participant* target, const Participant* caster,
        const Participant* player)
    {
        if (list.empty())
            return Verdict::Pass;
        ++mLists;
        bool unanswered = false;
        for (const survey::Ctda& c : list)
        {
            if (!answers(c))
            {
                unanswered = true;
                if (mUnansweredFunctions.insert(c.mFunction).second && mLog)
                    mLog("effect conditions: " + survey::functionName(c.mFunction) + " ("
                        + std::string(survey::xeditFunctionName(c.mFunction))
                        + ") cannot be answered here, the effects that use it do nothing");
            }
            else if (!runOnAnswered(c) || neededFor(c, target, caster, player) == nullptr)
            {
                unanswered = true;
                const std::uint32_t bit = 1u << std::min<std::uint32_t>(c.mRunOn, 7u);
                if ((mLoggedRunOn & bit) == 0)
                {
                    mLoggedRunOn |= bit;
                    if (mLog)
                        mLog("effect conditions: a condition runs on " + std::to_string(c.mRunOn)
                            + " which has nobody here, the effects that use it do nothing");
                }
            }
        }
        if (!unanswered && mExtended)
            for (const survey::Ctda& c : list)
                if ((c.mFlags & kUseGlobalFlag) != 0)
                {
                    float unused = 0.f;
                    if (!mGlobals || !mGlobals(static_cast<FormId>(c.mComparisonBits), unused))
                    {
                        unanswered = true;
                        if (mLog && mUnansweredFunctions.insert(c.mFunction).second)
                            mLog("effect conditions: " + survey::functionName(c.mFunction)
                                + " compares with a global the world does not know, the effects that use it do nothing");
                    }
                }
        if (unanswered)
        {
            ++mUnanswered;
            return Verdict::Unanswered;
        }
        World world;
        world.mTarget = target;
        world.mCaster = caster;
        world.mPlayer = player;
        world.mLookup = &mLookup;
        world.mSeed = mSeed;
        world.mCounter = &mCounter;
        world.mPlace = &mPlace;
        world.mGlobals = &mGlobals;
        const bool pass = mEvaluator.evaluate(list, world);
        ++(pass ? mPassed : mFailed);
        return pass ? Verdict::Pass : Verdict::Fail;
    }
}
