#include "game_skyrimse/barks.hpp"

#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/crime.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse::barks
{
    namespace
    {
        struct KindInfo
        {
            const char* mSubtype;
            const char* mName;
            bool mCombat;
        };

        // In the order of Kind.
        constexpr KindInfo kInfo[kKindCount] = {
            { "HELO", "hello", false },
            { "IDLE", "idle", false },
            { "NOTC", "engage", true },
            { "ALTC", "engage-alerted", true },
            { "ATCK", "attack", true },
            { "TAUT", "taunt", true },
            { "HIT_", "hit", true },
            { "FLEE", "flee", true },
            { "DETH", "death", true },
            { "LOTN", "lost", true },
            { "COTN", "calm", true },
            { "ASSA", "assault", true },
            { "STEA", "steal", true },
            { "TRES", "trespass", true },
            { "MURD", "murder", true },
            { "NOTA", "notice", false },
            { "PICN", "pickpocket", true },
            { "PICC", "pickpocket-combat", true },
        };

        constexpr int kReactionTries = 6; ///< lines tried for a reaction whose picks have no voice file (task 0235)

        std::uint64_t splitmix(std::uint64_t x)
        {
            x += 0x9e3779b97f4a7c15ull;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            return x ^ (x >> 31);
        }

        double horizontal(const Actor& a, double px, double py)
        {
            return std::hypot(a.mX - px, a.mY - py);
        }

        bool lessByReference(const Actor* a, const Actor* b)
        {
            return a->mReference < b->mReference;
        }
    }

    Kind kindOfCrime(crime::Kind crime)
    {
        switch (crime)
        {
            case crime::Kind::Theft:
                return Kind::Steal;
            case crime::Kind::Assault:
                return Kind::Assault;
            case crime::Kind::Murder:
                return Kind::Murder;
            case crime::Kind::Trespass:
                return Kind::Trespass;
            case crime::Kind::Pickpocket:
                return Kind::Pickpocket;
        }
        return Kind::Steal;
    }

    std::string_view subtypeOf(Kind kind)
    {
        return kind < Kind::Count ? kInfo[static_cast<std::size_t>(kind)].mSubtype : "";
    }

    std::string_view nameOf(Kind kind)
    {
        return kind < Kind::Count ? kInfo[static_cast<std::size_t>(kind)].mName : "";
    }

    bool isCombat(Kind kind)
    {
        return kind < Kind::Count && kInfo[static_cast<std::size_t>(kind)].mCombat;
    }

    Settings fromGameSettings(const SettingsTable* table)
    {
        Settings s;
        if (table == nullptr)
            return s;
        s.mHelloGap = table->get("fAIGreetingTimer", s.mHelloGap);
        s.mIdleMin = table->get("fIdleChatterCommentTimer", s.mIdleMin);
        s.mIdleMax = table->get("fIdleChatterCommentTimerMax", s.mIdleMax);
        s.mAttackMin = table->get("fCombatDialogueAttackMinElapsedTime", s.mAttackMin);
        s.mAttackMax = table->get("fCombatDialogueAttackMaxElapsedTime", s.mAttackMax);
        s.mTauntMin = table->get("fCombatDialogueTauntMinElapsedTime", s.mTauntMin);
        s.mTauntMax = table->get("fCombatDialogueTauntMaxElapsedTime", s.mTauntMax);
        s.mVoicePadding = table->get("fGameplayVoiceFilePadding", s.mVoicePadding);
        return s;
    }

    BarkSystem::BarkSystem(Settings settings, Hooks hooks)
        : mSettings(std::move(settings))
        , mHooks(std::move(hooks))
    {
        mQueue.reserve(mSettings.mMaxEvents);
    }

    bool BarkSystem::engaged(std::uint64_t reference) const
    {
        return mEngaged.count(reference) != 0;
    }

    void BarkSystem::onEvent(Kind kind, std::uint64_t reference, bool subjectIsVictim)
    {
        if (kind >= Kind::Count || kind == Kind::Hello || kind == Kind::Idle || kind == Kind::Attack
            || kind == Kind::Taunt)
            return; // these come from update
        if (mQueue.size() >= mSettings.mMaxEvents)
        {
            ++mStats.mEventsDropped;
            return;
        }
        mQueue.push_back({ kind, reference, subjectIsVictim });
    }

    void BarkSystem::onPlayerStance(bool weaponDrawn, bool sneaking)
    {
        mPlayerSneaking = sneaking;
        if (weaponDrawn && !mPlayerDrawn)
            mStanceEdge = true;
        mPlayerDrawn = weaponDrawn;
    }

    std::uint64_t BarkSystem::mix(std::uint64_t reference, std::uint32_t counter, Kind kind) const
    {
        return splitmix(mSettings.mSeed ^ splitmix(reference) ^ (static_cast<std::uint64_t>(counter) << 8)
            ^ static_cast<std::uint64_t>(kind));
    }

    float BarkSystem::draw(std::uint64_t reference, State& state, Kind kind, float low, float high) const
    {
        if (!(high > low))
            return low > 0.f ? low : 0.f;
        const double u = static_cast<double>(mix(reference, state.mCounter++, kind) >> 11) / 9007199254740992.0;
        return low + static_cast<float>(u) * (high - low);
    }

    bool BarkSystem::trySpeak(Kind kind, std::uint64_t reference, State& state, bool victim)
    {
        struct VictimScope
        {
            bool& mFlag;
            VictimScope(bool& flag, bool now)
                : mFlag(flag)
            {
                mFlag = now;
            }
            ~VictimScope() { mFlag = false; }
        } victimScope(mVictimNow, victim);
        Line line;
        // A reaction is said now or never, and many of the lines written for it have no voice file for this actor's
        // voice type, so it tries a few lines; the calm barks and the periodic fight cries retry on their own (the
        // fight cries every update while their timer is due: no trace line for those).
        const bool reaction = kind == Kind::Notice || kind == Kind::Steal || kind == Kind::Assault
            || kind == Kind::Trespass || kind == Kind::Murder || kind == Kind::Pickpocket
            || kind == Kind::PickpocketCombat;
        // Task 0235: a reaction that cannot be said leaves a line in the trace.
        const bool tracing = reaction && static_cast<bool>(mHooks.mTrace);
        const int tries = reaction ? kReactionTries : 1;
        for (int attempt = 0; attempt < tries; ++attempt)
        {
            const std::uint32_t pick = static_cast<std::uint32_t>(mix(reference, state.mCounter++, kind));
            line = Line();
            if (!mHooks.mChoose || !mHooks.mChoose(kind, reference, pick, line))
            {
                // Task 0243: a line gated by a random roll (GetRandomPercent) may pass on another pick, so a reaction
                // asks again; it is counted and traced once, when the last try fails
                if (mHooks.mChoose && attempt + 1 < tries)
                    continue;
                ++mStats.mNoLine;
                if (tracing)
                    mHooks.mTrace(
                        "BARK " + std::string(nameOf(kind)) + " " + std::to_string(reference) + " no line passes");
                return false;
            }
            if (!line.mVoice.empty())
                break;
            ++mStats.mNoVoice;
            if (tracing)
                mHooks.mTrace("BARK " + std::string(nameOf(kind)) + " " + std::to_string(reference)
                    + " no voice file for \"" + line.mText + "\"");
            if (attempt + 1 == tries)
                return false;
        }
        if (!mHooks.mSpeak || !mHooks.mSpeak(reference, line))
        {
            ++mStats.mRefused;
            if (tracing)
                mHooks.mTrace(
                    "BARK " + std::string(nameOf(kind)) + " " + std::to_string(reference) + " refused by the core");
            return false;
        }
        const float length
            = std::max(mSettings.mMinSpeech, static_cast<float>(line.mText.size()) * mSettings.mSecondsPerChar);
        const float until = mClock + length + mSettings.mVoicePadding;
        state.mSpeakingUntil = until;
        ++mStats.mSpoken[static_cast<std::size_t>(kind)];
        if (isCombat(kind))
        {
            mCombatFree = mClock + mSettings.mCombatGap;
            mCalmFree = std::max(mCalmFree, until);
        }
        else
            mCalmFree = until;
        if (mHooks.mTrace)
            mHooks.mTrace("BARK " + std::string(nameOf(kind)) + " " + std::to_string(reference) + " "
                + line.mVoice.front() + " \"" + line.mText + "\"");
        return true;
    }

    void BarkSystem::update(
        float seconds, const std::vector<Actor>& actors, bool havePlayer, double px, double py, double pz)
    {
        if (seconds > 0.f)
            mSim += seconds;
        if (mSim < mSettings.mStartAt || (mHooks.mReady && !mHooks.mReady()))
        {
            mQueue.clear(); // a moment of a fight before the data is read (or before the start time) is not kept
            return;
        }
        if (!mStarted)
        {
            // The bark clock starts here, at the caller's own seconds minus the start time: the same whenever the
            // data became ready before the start time.
            mStarted = true;
            mClock = std::max(0.f, mSim - mSettings.mStartAt);
        }
        else if (seconds > 0.f)
            mClock += seconds;
        if (!havePlayer)
        {
            mStats.mEventsDropped += mQueue.size();
            mQueue.clear();
            mStanceEdge = false; // the edge needs somebody to be near
            return;
        }
        const bool stanceEdge = mStanceEdge;
        mStanceEdge = false;
        mSorted.clear();
        for (const Actor& a : actors)
            mSorted.push_back(&a);
        std::sort(mSorted.begin(), mSorted.end(), lessByReference);
        auto find = [this](std::uint64_t reference) -> const Actor* {
            const auto it = std::lower_bound(mSorted.begin(), mSorted.end(), reference,
                [](const Actor* a, std::uint64_t r) { return a->mReference < r; });
            return it != mSorted.end() && (*it)->mReference == reference ? *it : nullptr;
        };
        auto canShout = [&](const Actor* actor, State& state, bool interrupt) {
            return actor != nullptr && horizontal(*actor, px, py) <= mSettings.mHearDistance && mClock >= mCombatFree
                && (interrupt || mClock >= state.mSpeakingUntil);
        };

        auto nextAttack = [this](std::uint64_t r, State& st) {
            return mClock + draw(r, st, Kind::Attack, mSettings.mAttackMin, mSettings.mAttackMax);
        };
        auto nextTaunt = [this](std::uint64_t r, State& st) {
            return mClock + draw(r, st, Kind::Taunt, mSettings.mTauntMin, mSettings.mTauntMax);
        };
        auto nextIdle = [this](std::uint64_t r, State& st) {
            return mClock + draw(r, st, Kind::Idle, mSettings.mIdleMin, mSettings.mIdleMax);
        };

        // 1. The events of the combat update, in the order they came.
        for (const Pending& event : mQueue)
        {
            State& state = mStates[event.mReference];
            const Actor* actor = find(event.mReference);
            switch (event.mKind)
            {
                case Kind::Engage:
                case Kind::EngageAlerted:
                {
                    Engaged& e = mEngaged[event.mReference];
                    e.mNextAttack = nextAttack(event.mReference, state);
                    e.mNextTaunt = nextTaunt(event.mReference, state);
                    if (canShout(actor, state, false))
                        (void)trySpeak(event.mKind, event.mReference, state);
                    break;
                }
                case Kind::Hit:
                    if (mClock >= state.mHitReady && canShout(actor, state, false)
                        && trySpeak(Kind::Hit, event.mReference, state))
                        state.mHitReady = mClock + mSettings.mHitGap;
                    break;
                case Kind::Death:
                    if (canShout(actor, state, true))
                        (void)trySpeak(Kind::Death, event.mReference, state);
                    mEngaged.erase(event.mReference);
                    break;
                case Kind::Lost:
                case Kind::Calm:
                    if (canShout(actor, state, false))
                        (void)trySpeak(event.mKind, event.mReference, state);
                    mEngaged.erase(event.mReference);
                    break;
                case Kind::Flee:
                case Kind::Assault:
                case Kind::Steal:
                case Kind::Trespass:
                case Kind::Murder:
                case Kind::Pickpocket:
                case Kind::PickpocketCombat:
                    if (canShout(actor, state, false))
                        (void)trySpeak(event.mKind, event.mReference, state, event.mVictim);
                    break;
                case Kind::Notice:
                    // Task 0235: "What was that?": not for an actor that is fighting, and not again within the gap.
                    if (!engaged(event.mReference) && mClock >= state.mNoticeReady && canShout(actor, state, false)
                        && trySpeak(Kind::Notice, event.mReference, state))
                        state.mNoticeReady = mClock + mSettings.mNoticeGap;
                    break;
                default:
                    break;
            }
        }
        mQueue.clear();

        // 2. The fighters' own timers: a blow cry and a taunt.
        for (auto& [reference, engaged] : mEngaged)
        {
            const Actor* actor = find(reference);
            if (actor == nullptr || actor->mDead)
                continue;
            State& state = mStates[reference];
            const bool heard = horizontal(*actor, px, py) <= mSettings.mHearDistance;
            if (mClock >= engaged.mNextTaunt)
            {
                if (!heard)
                    engaged.mNextTaunt = nextTaunt(reference, state);
                else if (canShout(actor, state, false))
                    // Task 0243: a cry that finds no line (or is refused) waits mNoLineGap, not the next update
                    engaged.mNextTaunt = trySpeak(Kind::Taunt, reference, state) ? nextTaunt(reference, state)
                                                                                 : mClock + mSettings.mNoLineGap;
            }
            if (mClock >= engaged.mNextAttack)
            {
                if (!heard)
                    engaged.mNextAttack = nextAttack(reference, state);
                else if (canShout(actor, state, false))
                    engaged.mNextAttack = trySpeak(Kind::Attack, reference, state) ? nextAttack(reference, state)
                                                                                   : mClock + mSettings.mNoLineGap;
            }
        }

        // 3. The calm barks: at most one a tick, a hello before a chat.
        const Actor* hello = nullptr;
        double helloDistance = 0.0;
        std::size_t stanceArmed = 0;
        const Actor* chat = nullptr;
        for (const Actor* actor : mSorted)
        {
            State& state = mStates[actor->mReference];
            const double flat = horizontal(*actor, px, py);
            const bool near = flat <= mSettings.mHelloDistance
                && std::abs(actor->mZ - pz) <= static_cast<double>(mSettings.mHelloHeight);
            if (!state.mSeen)
            {
                // First sight: whatever is near now was there before; nothing is said for it.
                state.mSeen = true;
                state.mNear = near;
                state.mNextIdle = nextIdle(actor->mReference, state);
                continue;
            }
            if (near && !state.mNear)
                state.mArmed = true;
            if (!near)
            {
                state.mArmed = false;
                state.mStanceArmed = false;
            }
            else if (stanceEdge && mClock >= state.mStanceReady)
            {
                state.mStanceArmed = true; // task 0235: a weapon was drawn near it
                ++stanceArmed;
            }
            state.mNear = near;
            if (actor->mDead || engaged(actor->mReference))
                continue;
            if (!mPlayerSneaking && ((state.mArmed && mClock >= state.mHelloReady) || state.mStanceArmed)
                && mClock >= mHelloFree && mClock >= state.mRetryAt && (hello == nullptr || flat < helloDistance))
            {
                hello = actor;
                helloDistance = flat;
            }
            if (chat == nullptr && mClock >= state.mNextIdle)
            {
                if (flat <= mSettings.mIdleDistance)
                    chat = actor;
                else
                    state.mNextIdle = nextIdle(actor->mReference, state);
            }
        }
        if (stanceEdge && mHooks.mTrace)
            mHooks.mTrace("BARK stance: a weapon or spell was drawn, " + std::to_string(stanceArmed)
                + " actor(s) near will remark on it");
        if (mClock >= mCalmFree)
        {
            if (hello != nullptr)
            {
                State& state = mStates[hello->mReference];
                if (mClock >= state.mSpeakingUntil && trySpeak(Kind::Hello, hello->mReference, state))
                {
                    if (state.mStanceArmed)
                        state.mStanceReady = mClock + mSettings.mStanceGap;
                    state.mArmed = false;
                    state.mStanceArmed = false;
                    state.mHelloReady = mClock + mSettings.mHelloReset;
                    mHelloFree = mClock + mSettings.mHelloGap;
                }
                else
                    state.mRetryAt = mClock + mSettings.mRetry;
            }
            else if (chat != nullptr)
            {
                State& state = mStates[chat->mReference];
                if (mClock >= state.mSpeakingUntil && trySpeak(Kind::Idle, chat->mReference, state))
                    state.mNextIdle = nextIdle(chat->mReference, state);
                else
                    state.mNextIdle = mClock + mSettings.mRetry;
            }
        }
    }
}
