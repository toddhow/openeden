// Task 0204: Skyrim crime for the player (see crime.hpp and docs/tasks/0204-skyrim-crime.md).
#include "game_skyrimse/crime.hpp"

#include "game_skyrimse/combat_factions.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game_skyrimse::crime
{
    namespace
    {
        using openeden::sdk::game::ActorHandle;
        using openeden::sdk::game::ActorSpeed;
        using openeden::sdk::game::CombatRequestWriter;
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::RayHit;
        using openeden::sdk::game::SubjectState;

        constexpr std::int32_t kMaxStolenCount = 1'000'000'000;
        constexpr std::size_t kMaxPending = 64;
        constexpr std::size_t kMaxAssaulted = 256;
        /// The safety net: a jail that Lua never ended is ended this long after the jail time.
        constexpr float kJailGrace = 60.f;

        std::string hex(FormId id)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out(8, '0');
            for (int i = 7; i >= 0; --i)
            {
                out[static_cast<std::size_t>(i)] = digits[id & 0xF];
                id >>= 4;
            }
            return out;
        }

        bool finite(double v)
        {
            return std::isfinite(v);
        }
    }

    std::string_view kindName(Kind kind)
    {
        switch (kind)
        {
            case Kind::Theft:
                return "theft";
            case Kind::Assault:
                return "assault";
            case Kind::Murder:
                return "murder";
            case Kind::Trespass:
                return "trespass";
        }
        return "";
    }

    std::int32_t bountyFor(Kind kind, const CrimeData& values, std::int32_t itemValue, std::int32_t count)
    {
        switch (kind)
        {
            case Kind::Murder:
                return values.mMurder;
            case Kind::Assault:
                return values.mAssault;
            case Kind::Trespass:
                return values.mTrespass;
            case Kind::Theft:
                break;
        }
        const float mult = values.mStealMultiplier;
        if (itemValue <= 0 || count <= 0 || !std::isfinite(mult) || mult <= 0.f)
            return 0;
        const double total = std::ceil(static_cast<double>(itemValue) * static_cast<double>(count)
            * static_cast<double>(mult));
        if (!(total < static_cast<double>(kMaxBounty)))
            return kMaxBounty;
        return std::max<std::int32_t>(1, static_cast<std::int32_t>(total));
    }

    // ---- Ledger ---------------------------------------------------------------------------------------------------

    bool Ledger::addBounty(FormId faction, std::int32_t gold)
    {
        if (faction == kNullFormId || gold <= 0)
            return false;
        const auto it = mBounty.find(faction);
        if (it == mBounty.end() && mBounty.size() >= kMaxLedgerEntries)
            return false;
        std::int64_t sum = static_cast<std::int64_t>(it == mBounty.end() ? 0 : it->second) + gold;
        sum = std::min<std::int64_t>(sum, kMaxBounty);
        mBounty[faction] = static_cast<std::int32_t>(sum);
        ++mRevision;
        return true;
    }

    std::int32_t Ledger::bounty(FormId faction) const
    {
        const auto it = mBounty.find(faction);
        return it == mBounty.end() ? 0 : it->second;
    }

    std::int64_t Ledger::totalBounty() const
    {
        std::int64_t sum = 0;
        for (const auto& [faction, gold] : mBounty)
            sum += gold;
        return sum;
    }

    void Ledger::clearBounty(FormId faction)
    {
        if (mBounty.erase(faction) != 0)
            ++mRevision;
    }

    void Ledger::clearAllBounties()
    {
        if (!mBounty.empty())
        {
            mBounty.clear();
            ++mRevision;
        }
    }

    bool Ledger::markStolen(FormId item, std::int32_t count)
    {
        if (item == kNullFormId || count <= 0)
            return false;
        const auto it = mStolen.find(item);
        if (it == mStolen.end() && mStolen.size() >= kMaxLedgerEntries)
            return false;
        const std::int64_t sum
            = std::min<std::int64_t>(static_cast<std::int64_t>(it == mStolen.end() ? 0 : it->second) + count,
                kMaxStolenCount);
        mStolen[item] = static_cast<std::int32_t>(sum);
        ++mRevision;
        return true;
    }

    std::int32_t Ledger::stolen(FormId item) const
    {
        const auto it = mStolen.find(item);
        return it == mStolen.end() ? 0 : it->second;
    }

    std::int64_t Ledger::stolenTotal() const
    {
        std::int64_t sum = 0;
        for (const auto& [item, count] : mStolen)
            sum += count;
        return sum;
    }

    std::int32_t Ledger::forgetStolen(FormId item, std::int32_t count)
    {
        const auto it = mStolen.find(item);
        if (it == mStolen.end() || count <= 0)
            return 0;
        const std::int32_t removed = std::min(it->second, count);
        it->second -= removed;
        if (it->second <= 0)
            mStolen.erase(it);
        ++mRevision;
        return removed;
    }

    void Ledger::clearStolen()
    {
        if (!mStolen.empty())
        {
            mStolen.clear();
            ++mRevision;
        }
    }

    void Ledger::clear()
    {
        mBounty.clear();
        mStolen.clear();
        ++mRevision;
    }

    void Ledger::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mBounty.size()));
        for (const auto& [faction, gold] : mBounty)
        {
            writer.writeValue(static_cast<std::uint32_t>(faction));
            writer.writeValue(static_cast<std::int32_t>(gold));
        }
        writer.writeValue(static_cast<std::uint32_t>(mStolen.size()));
        for (const auto& [item, count] : mStolen)
        {
            writer.writeValue(static_cast<std::uint32_t>(item));
            writer.writeValue(static_cast<std::int32_t>(count));
        }
    }

    namespace
    {
        // One block of (id, value) pairs, strictly ascending, id not 0, 1 <= value <= limit.
        bool readBlock(openeden::sdk::ChunkReader& reader, std::int32_t limit, std::map<FormId, std::int32_t>& out,
            std::string& error, const char* what)
        {
            std::uint32_t count = 0;
            if (!reader.readValue(count))
            {
                error = std::string(what) + ": the count is missing";
                return false;
            }
            if (count > kMaxLedgerEntries || static_cast<std::uint64_t>(count) * 8 > reader.remaining())
            {
                error = std::string(what) + ": the count does not fit";
                return false;
            }
            FormId previous = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t id = 0;
                std::int32_t value = 0;
                if (!reader.readValue(id) || !reader.readValue(value))
                {
                    error = std::string(what) + ": an entry is cut short";
                    return false;
                }
                if (id == 0 || (i > 0 && id <= previous) || value < 1 || value > limit)
                {
                    error = std::string(what) + ": an entry is out of order or out of range";
                    return false;
                }
                previous = id;
                out[id] = value;
            }
            return true;
        }
    }

    bool Ledger::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::map<FormId, std::int32_t> bounty;
        std::map<FormId, std::int32_t> stolen;
        if (!readBlock(reader, kMaxBounty, bounty, error, "bounties")
            || !readBlock(reader, kMaxStolenCount, stolen, error, "stolen items"))
            return false;
        if (reader.remaining() != 0)
        {
            error = "bytes follow the ledger";
            return false;
        }
        mBounty = std::move(bounty);
        mStolen = std::move(stolen);
        ++mRevision;
        return true;
    }

    bool LedgerChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
            mRefusal = "the crime chunk has version " + std::to_string(version) + ", expected 1";
        else
        {
            std::string error;
            if (mLedger.read(reader, error))
                return true;
            mRefusal = "the crime chunk is damaged: " + error;
        }
        if (mSink)
            mSink(mRefusal);
        return false;
    }

    // ---- witnesses ------------------------------------------------------------------------------------------------

    bool witnessSees(const Settings& settings, const CrimeActor& actor, double ax, double ay, double az, double px,
        double py, double pz, bool lineClear)
    {
        if (actor.mDead || actor.mEnemyOfPlayer || actor.mEngagedAgainstPlayer || !lineClear)
            return false;
        if (!finite(ax) || !finite(ay) || !finite(az) || !finite(px) || !finite(py) || !finite(pz))
            return false;
        if (std::hypot(ax - px, ay - py) > static_cast<double>(settings.mWitnessDistance))
            return false;
        return std::fabs(az - pz) <= static_cast<double>(settings.mWitnessHeight);
    }

    bool isCrimeVictim(const CrimeActor& victim)
    {
        return !victim.mDead && !victim.mEnemyOfPlayer && !victim.mEngagedAgainstPlayer;
    }

    std::string_view stateName(State state)
    {
        switch (state)
        {
            case State::None:
                return "none";
            case State::Approach:
                return "approach";
            case State::Offer:
                return "offer";
            case State::Jailed:
                return "jailed";
            case State::Resisted:
                return "resisted";
        }
        return "";
    }

    // ---- the system -------------------------------------------------------------------------------------------

    CrimeSystem::CrimeSystem(Ledger& ledger, CrimeWorld& world, Settings settings)
        : mLedger(ledger)
        , mWorld(world)
        , mSettings(settings)
    {
        mPending.reserve(kMaxPending);
        mAssaulted.reserve(kMaxAssaulted);
        mRoster.reserve(128);
        mScratch.reserve(128);
    }

    void CrimeSystem::note(std::string text)
    {
        mWorld.log("CRIME " + text);
    }

    void CrimeSystem::onTheft(FormId item, std::int32_t count)
    {
        if (item == kNullFormId || count <= 0)
            return;
        (void)mLedger.markStolen(item, count);
        if (mPending.size() < kMaxPending)
            mPending.push_back({ Kind::Theft, item, count, {} });
    }

    void CrimeSystem::onDamage(const openeden::sdk::game::DamageReport& report)
    {
        if (report.victimIsPlayer && !report.attackerIsPlayer && report.attacker.valid() && report.amount > 0.f)
        {
            // An NPC that hurt the player fought back: killing it later is self defence, not murder.
            if (std::find(mFoughtBack.begin(), mFoughtBack.end(), report.attacker) == mFoughtBack.end())
            {
                if (mFoughtBack.size() >= kMaxAssaulted)
                    mFoughtBack.erase(mFoughtBack.begin());
                mFoughtBack.push_back(report.attacker);
            }
            return;
        }
        if (!report.attackerIsPlayer || report.victimIsPlayer || !report.victim.valid() || !(report.amount > 0.f))
            return;
        if (std::find(mAssaulted.begin(), mAssaulted.end(), report.victim) != mAssaulted.end())
            return;
        CrimeActor victim;
        if (!mWorld.actor(report.victim, victim) || !isCrimeVictim(victim))
            return;
        if (mAssaulted.size() >= kMaxAssaulted)
            mAssaulted.erase(mAssaulted.begin());
        mAssaulted.push_back(report.victim);
        if (report.healthAfter > 0.f && mPending.size() < kMaxPending)
            mPending.push_back({ Kind::Assault, kNullFormId, 0, report.victim });
    }

    void CrimeSystem::onDeath(openeden::sdk::game::ActorHandle actor)
    {
        const auto it = std::find(mAssaulted.begin(), mAssaulted.end(), actor);
        if (it == mAssaulted.end())
            return;
        mAssaulted.erase(it);
        const auto foughtBack = std::find(mFoughtBack.begin(), mFoughtBack.end(), actor);
        if (foughtBack != mFoughtBack.end())
        {
            mFoughtBack.erase(foughtBack);
            return; // it hurt the player first: self defence, no murder
        }
        if (mPending.size() < kMaxPending)
            mPending.push_back({ Kind::Murder, kNullFormId, 0, actor });
    }

    void CrimeSystem::evaluate(CombatRequestWriter& writer, const Pending& crime)
    {
        SubjectState player;
        if (!writer.subjectState(CombatSubject::ofPlayer(), player))
            return;
        mRoster.clear();
        mWorld.actors(mRoster);
        struct Candidate
        {
            double mDistance;
            std::size_t mIndex;
            SubjectState mState;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(mRoster.size());
        for (std::size_t i = 0; i < mRoster.size(); ++i)
        {
            CrimeActor a = mRoster[i];
            if (a.mHandle == crime.mVictim)
            {
                // it was a civilian when it was hit: the report was made before it fought back
                a.mEnemyOfPlayer = false;
                a.mEngagedAgainstPlayer = false;
            }
            SubjectState s;
            if (!writer.subjectState(CombatSubject::of(a.mHandle), s))
                continue;
            if (!witnessSees(mSettings, a, s.x, s.y, s.z, player.x, player.y, player.z, true))
                continue;
            mRoster[i] = a;
            candidates.push_back({ std::hypot(s.x - player.x, s.y - player.y), i, s });
        }
        std::sort(candidates.begin(), candidates.end(), [this](const Candidate& a, const Candidate& b) {
            if (a.mDistance != b.mDistance)
                return a.mDistance < b.mDistance;
            return mRoster[a.mIndex].mHandle.reference < mRoster[b.mIndex].mHandle.reference;
        });
        std::vector<FormId> factions;
        std::string who;
        std::size_t rays = 0;
        for (const Candidate& c : candidates)
        {
            if (rays >= mSettings.mMaxRays)
                break;
            const CrimeActor& a = mRoster[c.mIndex];
            bool clear = true;
            const Factions::SightRay ray
                = Factions::sightRay(c.mState.x, c.mState.y, c.mState.z, player.x, player.y, player.z);
            if (ray.mValid)
            {
                ++rays;
                ++mRays;
                RayHit hit;
                if (writer.castRay(CombatSubject::of(a.mHandle), ray.mRay, hit))
                    clear = Factions::sightClear(hit.fate);
            }
            if (!clear)
                continue;
            if (a.mCrimeFaction == kNullFormId || mWorld.crimeData(a.mCrimeFaction) == nullptr)
                continue;
            if (std::find(factions.begin(), factions.end(), a.mCrimeFaction) == factions.end())
            {
                factions.push_back(a.mCrimeFaction);
                who += (who.empty() ? "" : ",") + (a.mName.empty() ? hex(a.mBase) : a.mName);
            }
        }
        if (factions.empty())
        {
            note(std::string(kindName(crime.mKind)) + " not witnessed");
            return;
        }
        std::sort(factions.begin(), factions.end());
        const std::int32_t value = crime.mKind == Kind::Theft ? mWorld.itemValue(crime.mItem) : 0;
        for (const FormId faction : factions)
        {
            const CrimeData* data = mWorld.crimeData(faction);
            const std::int32_t gold = bountyFor(crime.mKind, *data, value, crime.mCount);
            if (gold <= 0)
                continue;
            (void)mLedger.addBounty(faction, gold);
            note(std::string(kindName(crime.mKind)) + " witnessed by " + who + " faction " + hex(faction)
                + " bounty +" + std::to_string(gold) + " total " + std::to_string(mLedger.bounty(faction)));
            ++mRevision;
        }
    }

    void CrimeSystem::update(float seconds, CombatRequestWriter& writer)
    {
        if (seconds > 0.f && std::isfinite(seconds))
            mClock += seconds;
        if (!mPending.empty())
        {
            std::vector<Pending> work;
            work.swap(mPending);
            for (const Pending& crime : work)
                evaluate(writer, crime);
            mPending.reserve(kMaxPending);
        }
        runGuards(writer);
    }

    void CrimeSystem::finishOffer(float cooldown)
    {
        if (mFaction != kNullFormId)
            mCooldownUntil[mFaction] = mClock + cooldown;
        mState = State::None;
        mGuard = {};
        mGuardName.clear();
        mStateSince = mClock;
        ++mRevision;
    }

    void CrimeSystem::confiscate()
    {
        const std::map<FormId, std::int32_t> stolen = mLedger.stolenItems();
        for (const auto& [item, count] : stolen)
            (void)mWorld.takeItem(item, count);
        mLedger.clearStolen();
    }

    bool CrimeSystem::canPay() const
    {
        return mState == State::Offer && mWorld.playerGold() >= mLedger.bounty(mFaction);
    }

    void CrimeSystem::runGuards(CombatRequestWriter& writer)
    {
        if (mState == State::Jailed)
        {
            if (mClock > mJailUntil + kJailGrace)
                released();
            return;
        }
        if (mState == State::Resisted)
        {
            if (mClock - mStateSince >= mSettings.mOfferCooldown)
                finishOffer(0.f);
            return;
        }
        if (mState == State::None && mLedger.bounties().empty())
            return;
        SubjectState player;
        if (!writer.subjectState(CombatSubject::ofPlayer(), player))
            return;

        // The guards of a faction near the player, nearest first.
        const auto guardsOf = [&](FormId faction, std::vector<std::pair<double, std::size_t>>& out) {
            out.clear();
            mRoster.clear();
            mWorld.actors(mRoster);
            for (std::size_t i = 0; i < mRoster.size(); ++i)
            {
                const CrimeActor& a = mRoster[i];
                if (!a.mGuard || a.mDead || a.mCrimeFaction != faction)
                    continue;
                SubjectState s;
                if (!writer.subjectState(CombatSubject::of(a.mHandle), s))
                    continue;
                const double d = std::hypot(s.x - player.x, s.y - player.y);
                if (d <= static_cast<double>(mSettings.mGuardCallRadius))
                    out.emplace_back(d, i);
            }
            std::sort(out.begin(), out.end(), [this](const auto& a, const auto& b) {
                if (a.first != b.first)
                    return a.first < b.first;
                return mRoster[a.second].mHandle.reference < mRoster[b.second].mHandle.reference;
            });
        };

        if (mState == State::None)
        {
            for (const auto& [faction, gold] : mLedger.bounties())
            {
                const auto rest = mCooldownUntil.find(faction);
                if (rest != mCooldownUntil.end() && mClock < rest->second)
                    continue;
                guardsOf(faction, mScratch);
                if (mScratch.empty())
                    continue;
                mFaction = faction;
                mStateSince = mClock;
                ++mRevision;
                const CrimeActor guard = mRoster[mScratch.front().second];
                if (gold >= mSettings.mAttackOnSightBounty)
                {
                    mState = State::Resisted;
                    for (const auto& entry : mScratch)
                        mWorld.guardAttack(mRoster[entry.second].mHandle);
                    mCooldownUntil[faction] = mClock + mSettings.mOfferCooldown;
                    note("guards attack on sight faction " + hex(faction) + " bounty " + std::to_string(gold));
                    return;
                }
                mGuard = guard.mHandle;
                mGuardName = guard.mName;
                mState = State::Approach;
                mLastGoalAt = -1.0e9f;
                note("guard " + guard.mName + " comes for faction " + hex(faction) + " bounty "
                    + std::to_string(gold));
                break;
            }
            if (mState != State::Approach)
                return;
        }

        if (mState == State::Approach)
        {
            CrimeActor guard;
            SubjectState gs;
            if (!mWorld.actor(mGuard, guard) || guard.mDead || !writer.subjectState(CombatSubject::of(mGuard), gs))
            {
                guardsOf(mFaction, mScratch);
                if (mScratch.empty())
                {
                    note("the guard is gone, no other comes");
                    finishOffer(mSettings.mOfferCooldown);
                    return;
                }
                const CrimeActor& next = mRoster[mScratch.front().second];
                mGuard = next.mHandle;
                mGuardName = next.mName;
                mLastGoalAt = -1.0e9f;
                if (!writer.subjectState(CombatSubject::of(mGuard), gs))
                    return;
            }
            const double distance = std::hypot(gs.x - player.x, gs.y - player.y);
            if (distance <= static_cast<double>(mSettings.mTalkDistance))
            {
                mWorld.guardHold(mGuard);
                mState = State::Offer;
                mStateSince = mClock;
                mMessage = "You have a bounty of " + std::to_string(mLedger.bounty(mFaction)) + " gold in "
                    + mWorld.factionName(mFaction) + ".";
                ++mRevision;
                note("offer by " + mGuardName + " faction " + hex(mFaction) + " bounty "
                    + std::to_string(mLedger.bounty(mFaction)));
                return;
            }
            if (mClock - mStateSince > mSettings.mApproachTimeout)
            {
                note("the guard did not arrive");
                mWorld.guardRelease(mGuard);
                finishOffer(mSettings.mOfferCooldown);
                return;
            }
            const bool moved = std::hypot(player.x - mGoalX, player.y - mGoalY)
                >= static_cast<double>(mSettings.mRetargetDistance);
            if (mLastGoalAt < -1.0e8f || (moved && mClock - mLastGoalAt >= mSettings.mRetargetPeriod))
            {
                mWorld.guardGoTo(mGuard, player.x, player.y, player.z,
                    distance > static_cast<double>(mSettings.mRunDistance) ? ActorSpeed::Run : ActorSpeed::Walk);
                mLastGoalAt = mClock;
                mGoalX = player.x;
                mGoalY = player.y;
            }
            return;
        }

        if (mState == State::Offer)
        {
            CrimeActor guard;
            if (!mWorld.actor(mGuard, guard) || guard.mDead)
            {
                note("the guard left the offer");
                finishOffer(mSettings.mOfferCooldown);
                return;
            }
            if (mClock - mStateSince > mSettings.mOfferTimeout)
            {
                note("the offer lapsed");
                mWorld.guardRelease(mGuard);
                finishOffer(mSettings.mOfferCooldown);
            }
        }
    }

    ChoiceStatus CrimeSystem::choose(std::string_view choice)
    {
        if (choice == "pay")
            return this->choose(Choice::Pay);
        if (choice == "jail")
            return this->choose(Choice::Jail);
        if (choice == "resist")
            return this->choose(Choice::Resist);
        return ChoiceStatus::Unknown;
    }

    ChoiceStatus CrimeSystem::choose(Choice choice)
    {
        if (mState != State::Offer)
            return ChoiceStatus::NoOffer;
        const std::int32_t bounty = mLedger.bounty(mFaction);
        switch (choice)
        {
            case Choice::Pay:
                if (mWorld.playerGold() < bounty || !mWorld.takeGold(bounty))
                {
                    mMessage = "You do not have enough gold.";
                    ++mRevision;
                    return ChoiceStatus::NotEnoughGold;
                }
                mLedger.clearBounty(mFaction);
                confiscate();
                note("paid " + std::to_string(bounty) + " faction " + hex(mFaction));
                mWorld.guardRelease(mGuard);
                finishOffer(mSettings.mOfferCooldown);
                return ChoiceStatus::Ok;
            case Choice::Jail:
                mLedger.clearBounty(mFaction);
                confiscate();
                note("jailed faction " + hex(mFaction));
                mWorld.guardRelease(mGuard);
                mJailFaction = mFaction;
                mJailUntil = mClock + mSettings.mJailSeconds;
                mState = State::Jailed;
                mStateSince = mClock;
                mGuard = {};
                ++mRevision;
                return ChoiceStatus::Ok;
            case Choice::Resist:
            {
                // Every loaded guard of the faction fights; the chosen one first.
                mWorld.guardAttack(mGuard);
                mRoster.clear();
                mWorld.actors(mRoster);
                for (const CrimeActor& a : mRoster)
                    if (a.mGuard && !a.mDead && a.mCrimeFaction == mFaction && !(a.mHandle == mGuard))
                        mWorld.guardAttack(a.mHandle);
                note("resisted faction " + hex(mFaction) + " bounty " + std::to_string(bounty));
                mCooldownUntil[mFaction] = mClock + mSettings.mOfferCooldown;
                mState = State::Resisted;
                mStateSince = mClock;
                mGuard = {};
                mGuardName.clear();
                ++mRevision;
                return ChoiceStatus::Ok;
            }
        }
        return ChoiceStatus::Ok;
    }

    void CrimeSystem::setMessage(std::string text)
    {
        mMessage = std::move(text);
        ++mRevision;
    }

    void CrimeSystem::released()
    {
        if (mState != State::Jailed)
            return;
        note("released faction " + hex(mJailFaction));
        mFaction = mJailFaction;
        mJailFaction = kNullFormId;
        finishOffer(mSettings.mOfferCooldown);
    }
}
