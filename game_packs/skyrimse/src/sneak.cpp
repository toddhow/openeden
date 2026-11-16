#include "game_skyrimse/sneak.hpp"

#include "game_skyrimse/combat_factions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

// Sneaking and detection: see the header (task 0227). Standard library only; no clock, no random numbers.

namespace game_skyrimse::sneak
{
    namespace
    {
        /// The Sneak skill of a new character when no progression gives one (RECALLED-VERIFY).
        constexpr float kStandInSkill = 15.f;
        /// A speed above this between two judgements is a teleport or a load, not a walk: it counts as 0. OURS.
        constexpr double kMaxPlausibleSpeed = 1000.0;
        constexpr double kPi = 3.14159265358979323846;

        bool finite(float v)
        {
            return std::isfinite(v);
        }

        std::string fmt(double v, int decimals)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
            return buffer;
        }

        double wrapAngle(double a)
        {
            while (a > kPi)
                a -= 2.0 * kPi;
            while (a < -kPi)
                a += 2.0 * kPi;
            return a;
        }
    }

    std::string_view alertName(Alert alert)
    {
        switch (alert)
        {
            case Alert::Hidden:
                return "hidden";
            case Alert::Caution:
                return "caution";
            case Alert::Detected:
                return "detected";
        }
        return "hidden";
    }

    // ---- the pure rules ------------------------------------------------------------------------------------------

    float stealthRange(const Settings& settings, const PlayerStealth& player)
    {
        const float skill = finite(player.mSkill) ? std::clamp(player.mSkill, 0.f, 100.f) : kStandInSkill;
        float speed = 0.f;
        if (std::isnan(player.mSpeed))
            speed = 0.f;
        else if (std::isinf(player.mSpeed))
            speed = player.mSpeed > 0.f ? settings.mRunSpeed : 0.f;
        else
            speed = std::max(0.f, player.mSpeed);
        const float weight = finite(player.mArmourWeight) ? std::max(0.f, player.mArmourWeight) : 0.f;
        const float skillFactor = 1.f - settings.mSkillMult * skill / 100.f;
        const float motion = speed >= settings.mRunSpeed ? settings.mRunningMult
            : speed < settings.mStillSpeed               ? settings.mStillFactor
                                                         : 1.f;
        const float armour = 1.f + weight * settings.mArmourNoisePerWeight;
        const float stance = player.mSneaking ? 1.f : settings.mActionMult;
        const float range = settings.mMaxDistance * settings.mRangeScale * skillFactor * motion * armour * stance;
        return finite(range) ? std::max(0.f, range) : 0.f;
    }

    float updateNotice(const Settings& settings, float notice, float seconds, float range, float distance, bool sees)
    {
        const float before = finite(notice) ? std::clamp(notice, 0.f, settings.mNoticeMax) : 0.f;
        if (!(seconds > 0.f) || !finite(seconds))
            return before;
        float n = before;
        const float effective = sees ? range : range * settings.mSoundLosMult;
        if (finite(distance) && finite(effective) && effective > 0.f && distance < effective)
        {
            n += settings.mNoticeRate * (1.f - distance / effective) * seconds;
            if (!sees)
                n = std::min(n, std::max(settings.mHeardCap, before));
        }
        else
            n -= settings.mDecayRate * seconds;
        return std::clamp(n, 0.f, settings.mNoticeMax);
    }

    Alert classify(const Settings& settings, float notice, Alert before)
    {
        if (notice >= settings.mDetectEnter)
            return Alert::Detected;
        if (before == Alert::Detected && notice >= settings.mDetectLeave)
            return Alert::Detected;
        if (notice >= settings.mCautionEnter)
            return Alert::Caution;
        if (before == Alert::Caution && notice >= settings.mCautionLeave)
            return Alert::Caution;
        return Alert::Hidden;
    }

    float baseAttackMultiplier(const Settings& settings, std::uint8_t animType)
    {
        switch (animType)
        {
            case WeaponAnimType::OneHandSword:
                return settings.mMult1HSword;
            case WeaponAnimType::OneHandDagger:
                return settings.mMult1HDagger;
            case WeaponAnimType::OneHandAxe:
                return settings.mMult1HAxe;
            case WeaponAnimType::OneHandMace:
                return settings.mMult1HMace;
            case WeaponAnimType::TwoHandSword:
                return settings.mMult2HSword;
            case WeaponAnimType::TwoHandAxe:
                return settings.mMult2HAxe;
            case WeaponAnimType::Bow:
            case WeaponAnimType::Crossbow:
                return settings.mMultBow;
            default:
                return settings.mMultHand; // bare hands, a staff, anything unknown
        }
    }

    std::string attackMessage(float multiplier)
    {
        const float m = (finite(multiplier) && multiplier > 0.f) ? multiplier : 1.f;
        std::string number = fmt(static_cast<double>(m), 2);
        while (!number.empty() && number.back() == '0')
            number.pop_back();
        if (!number.empty() && number.back() == '.')
            number.pop_back();
        return "Sneak attack for " + number + "x damage!";
    }

    float hiddenTraining(
        const Settings& settings, bool sneaking, float speed, std::size_t hiddenWatchers, float seconds)
    {
        if (!sneaking || hiddenWatchers == 0 || !finite(speed) || speed < settings.mStillSpeed || !finite(seconds)
            || !(seconds > 0.f))
            return 0.f;
        return settings.mSkillUsagePerSecond * seconds;
    }

    float applySneakAttackPerks(PerkEngine& engine, std::span<const FormId> perks, const PerkParticipant& owner,
        const PerkParticipant* weapon, const PerkParticipant& target, float multiplier, std::size_t* applied)
    {
        if (applied != nullptr)
            *applied = 0;
        if (!finite(multiplier) || !(multiplier > 0.f) || !engine.hasEntryPoint(perks, PerkEntry::ModSneakAttackMult))
            return multiplier;
        PerkParticipant sneaking = owner;
        sneaking.mSneaking = true;
        PerkQuery query;
        query.set(PerkRole::Owner, &sneaking);
        query.set(PerkRole::Target, &target);
        if (weapon != nullptr)
            query.set(PerkRole::Weapon, weapon);
        const EntryOutcome outcome = engine.apply(PerkEntry::ModSneakAttackMult, perks, query, multiplier);
        if (outcome.mApplied == 0 || !finite(outcome.mValue))
            return multiplier;
        if (applied != nullptr)
            *applied = outcome.mApplied;
        return std::max(outcome.mValue, 0.f);
    }

    // ---- the system --------------------------------------------------------------------------------------------------

    SneakSystem::SneakSystem(Hooks hooks, Settings settings)
        : mHooks(std::move(hooks))
        , mSettings(settings)
    {
        mObservers.reserve(64);
        mRoster.reserve(64);
        mScratch.reserve(64);
        mNext.reserve(64);
    }

    SneakSystem::~SneakSystem() = default;

    int SneakSystem::eye() const
    {
        return mSneaking ? static_cast<int>(mOverall) : -1;
    }

    SneakSystem::Observer* SneakSystem::find(std::uint32_t reference)
    {
        const auto it = std::lower_bound(mObservers.begin(), mObservers.end(), reference,
            [](const Observer& o, std::uint32_t r) { return o.mReference < r; });
        return it != mObservers.end() && it->mReference == reference ? &*it : nullptr;
    }

    const SneakSystem::Observer* SneakSystem::find(std::uint32_t reference) const
    {
        const auto it = std::lower_bound(mObservers.begin(), mObservers.end(), reference,
            [](const Observer& o, std::uint32_t r) { return o.mReference < r; });
        return it != mObservers.end() && it->mReference == reference ? &*it : nullptr;
    }

    Alert SneakSystem::alertOf(std::uint32_t reference) const
    {
        const Observer* o = find(reference);
        return o != nullptr ? o->mAlert : Alert::Hidden;
    }

    void SneakSystem::note(const std::string& text)
    {
        if (mHooks.mLog)
            mHooks.mLog(text);
    }

    void SneakSystem::markDetected(std::uint32_t reference)
    {
        Observer* o = find(reference);
        if (o == nullptr)
        {
            const auto it = std::lower_bound(mObservers.begin(), mObservers.end(), reference,
                [](const Observer& x, std::uint32_t r) { return x.mReference < r; });
            Observer fresh;
            fresh.mReference = reference;
            o = &*mObservers.insert(it, fresh);
        }
        o->mNotice = mSettings.mNoticeMax;
        o->mAlert = Alert::Detected;
        mOverall = Alert::Detected;
    }

    float SneakSystem::attackMultiplier(std::uint32_t victim, std::uint8_t animType, bool ranged) const
    {
        if (!mSneaking || alertOf(victim) == Alert::Detected)
            return 1.f;
        return ranged ? mSettings.mMultBow : baseAttackMultiplier(mSettings, animType);
    }

    void SneakSystem::onSneakAttack(std::uint32_t victim, float multiplier)
    {
        mMessage = attackMessage(multiplier);
        ++mMessageId;
        markDetected(victim);
        if (mHooks.mTrain)
            mHooks.mTrain(mSettings.mAttackSkillUsage);
        note("SNEAK attack on " + std::to_string(victim) + " x" + fmt(static_cast<double>(multiplier), 2));
    }

    void SneakSystem::update(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        if (!(seconds > 0.f) || !finite(seconds))
            return;
        mSince += seconds;
        if (mSince + 1.0e-4f < mSettings.mPeriod)
            return;
        const float step = mSince;
        mSince = 0.f;
        judge(step, writer);
    }

    void SneakSystem::judge(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        using openeden::sdk::game::CombatSubject;
        using openeden::sdk::game::RayHit;
        using openeden::sdk::game::SubjectState;

        SubjectState player;
        if (!writer.subjectState(CombatSubject::ofPlayer(), player))
        {
            mSneaking = false;
            mHaveLast = false;
            mPrevSpeed = 0.f;
            mOverall = Alert::Hidden;
            mObservers.clear();
            return;
        }
        ++mJudgements;
        mSneaking = player.sneaking;
        // The speed is the slower of the last two intervals: one fast interval is a teleport or a stumble, not a run.
        float interval = 0.f;
        if (mHaveLast)
        {
            const double v = std::hypot(player.x - mLastX, player.y - mLastY) / static_cast<double>(seconds);
            interval = v <= kMaxPlausibleSpeed ? static_cast<float>(v) : 0.f;
        }
        mSpeed = std::min(interval, mPrevSpeed);
        mPrevSpeed = interval;
        mLastX = player.x;
        mLastY = player.y;
        mHaveLast = true;

        PlayerStealth stealth;
        stealth.mSneaking = mSneaking;
        stealth.mSkill = mHooks.mSkill ? mHooks.mSkill() : kStandInSkill;
        stealth.mSpeed = mSpeed;
        stealth.mArmourWeight = mHooks.mArmourWeight ? mHooks.mArmourWeight() : 0.f;
        mRange = stealthRange(mSettings, stealth);

        mRoster.clear();
        if (mHooks.mActors)
            mHooks.mActors(mRoster);
        mScratch.clear();
        for (std::size_t i = 0; i < mRoster.size(); ++i)
        {
            const crime::CrimeActor& a = mRoster[i];
            if (a.mDead || (mHooks.mIsFollower && mHooks.mIsFollower(a.mHandle.reference)))
                continue;
            SubjectState s;
            if (!writer.subjectState(CombatSubject::of(a.mHandle), s))
                continue;
            const double d = std::sqrt((s.x - player.x) * (s.x - player.x) + (s.y - player.y) * (s.y - player.y)
                + (s.z - player.z) * (s.z - player.z));
            if (!(d <= static_cast<double>(mSettings.mConsiderDistance)))
                continue;
            Candidate c;
            c.mDistance = d;
            c.mIndex = i;
            c.mState = s;
            mScratch.push_back(c);
        }
        std::sort(mScratch.begin(), mScratch.end(), [this](const Candidate& a, const Candidate& b) {
            if (a.mDistance != b.mDistance)
                return a.mDistance < b.mDistance;
            return mRoster[a.mIndex].mHandle.reference < mRoster[b.mIndex].mHandle.reference;
        });

        mNext.clear();
        std::size_t rays = 0;
        std::size_t hidden = 0;
        Alert overall = Alert::Hidden;
        for (const Candidate& c : mScratch)
        {
            const crime::CrimeActor& a = mRoster[c.mIndex];
            const std::uint32_t reference = a.mHandle.reference;
            Observer o;
            o.mReference = reference;
            if (const Observer* old = find(reference))
                o = *old;
            const Alert was = o.mAlert;
            if (a.mEngagedAgainstPlayer)
            {
                o.mNotice = mSettings.mNoticeMax;
                o.mAlert = Alert::Detected;
            }
            else
            {
                const double dx = player.x - c.mState.x;
                const double dy = player.y - c.mState.y;
                const bool near2d = std::hypot(dx, dy) < 1.0;
                const double facing = std::fabs(wrapAngle(static_cast<double>(c.mState.heading) - std::atan2(dx, dy)));
                const bool inView = near2d || facing <= static_cast<double>(mSettings.mHalfView);
                bool sees = false;
                if (inView && c.mDistance <= static_cast<double>(mRange))
                {
                    if (rays < mSettings.mMaxRays)
                    {
                        const Factions::SightRay ray = Factions::sightRay(
                            c.mState.x, c.mState.y, c.mState.z, player.x, player.y, player.z);
                        if (ray.mValid)
                        {
                            ++rays;
                            RayHit hit;
                            sees = writer.castRay(CombatSubject::of(a.mHandle), ray.mRay, hit)
                                ? Factions::sightClear(hit.fate)
                                : true;
                        }
                        else
                            sees = true;
                    }
                }
                o.mNotice = updateNotice(mSettings, o.mNotice, seconds, mRange, static_cast<float>(c.mDistance), sees);
                o.mAlert = classify(mSettings, o.mNotice, o.mAlert);
            }
            if (o.mAlert != was)
                note("SNEAK " + (a.mName.empty() ? std::to_string(reference) : a.mName) + " " + std::string(alertName(was))
                    + " -> " + std::string(alertName(o.mAlert)) + " notice " + fmt(o.mNotice, 2) + " distance "
                    + fmt(c.mDistance, 1) + " range " + fmt(mRange, 1) + " speed " + fmt(mSpeed, 0));
            if (o.mAlert != Alert::Detected)
                ++hidden;
            if (static_cast<int>(o.mAlert) > static_cast<int>(overall))
                overall = o.mAlert;
            mNext.push_back(o);
        }
        mRays += rays;
        std::sort(mNext.begin(), mNext.end(),
            [](const Observer& a, const Observer& b) { return a.mReference < b.mReference; });
        mObservers.swap(mNext);
        mOverall = overall;

        const float training = hiddenTraining(mSettings, mSneaking, mSpeed, hidden, seconds);
        if (training > 0.f && mHooks.mTrain)
            mHooks.mTrain(training);
    }

    // ---- the script package --------------------------------------------------------------------------------------------

    namespace
    {
        using openeden::sdk::BindingAccess;
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult()>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue>) override { return mBody(); }

        private:
            Body mBody;
        };
    }

    struct SneakBindings::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
    };

    SneakBindings::SneakBindings(const SneakSystem& system)
        : mImpl(std::make_unique<Impl>())
    {
        Impl& m = *mImpl;
        const SneakSystem* sys = &system;
        const auto add = [&m](std::string_view name, BindingType result, Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.hasResult = true;
            f.result = result;
            f.access = BindingAccess::Read;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        add("eye", BindingType::Int32, [sys]() {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = sys->eye();
            return r;
        });
        add("message", BindingType::String, [sys]() {
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.text = sys->message();
            return r;
        });
        add("message_id", BindingType::Int32, [sys]() {
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(sys->messageId() & 0x7FFFFFFFu);
            return r;
        });
        add("message_seconds", BindingType::Double, [sys]() {
            BindingResult r;
            r.value.type = BindingType::Double;
            r.value.number = static_cast<double>(sys->settings().mMessageSeconds);
            return r;
        });
    }

    SneakBindings::~SneakBindings() = default;

    openeden::sdk::DeclareResult SneakBindings::declareTo(openeden::sdk::BindingsService& service)
    {
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = openeden::sdk::Version{ 1, 0 };
        ns.functions = mImpl->mFunctions;
        ns.menuSafe = false;
        return service.declareNamespace(ns);
    }
}
