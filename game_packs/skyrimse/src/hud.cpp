#include <game_skyrimse/hud.hpp>

#include <cmath>

namespace game_skyrimse
{
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::DamageReport;

    namespace
    {
        // A number the screen may show: finite and not below 0, else 0.
        float clean(float value)
        {
            return std::isfinite(value) && value > 0.f ? value : 0.f;
        }

        std::string cutName(std::string_view name)
        {
            std::size_t cut = name.size();
            if (cut > kMaxHudNameBytes)
            {
                cut = kMaxHudNameBytes;
                // The first dropped byte is the middle of a character: drop the whole character.
                while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
                    --cut;
            }
            return std::string(name.substr(0, cut));
        }
    }

    CombatHud::CombatHud()
    {
        mActors.reserve(kMaxHudActors);
        mEnemyName.reserve(kMaxHudNameBytes);
        mSnapshot.mPlayerMagicka = kHudMagickaStandIn;
        mSnapshot.mPlayerMagickaMax = kHudMagickaStandIn;
    }

    void CombatHud::actorAdded(ActorHandle handle, std::string_view name)
    {
        if (!handle.valid())
            return;
        for (Entry& entry : mActors)
        {
            if (entry.mHandle == handle)
            {
                entry.mName = cutName(name);
                return;
            }
        }
        if (mActors.size() >= kMaxHudActors)
            return;
        mActors.push_back(Entry{ handle, cutName(name) });
    }

    void CombatHud::actorRemoved(ActorHandle handle)
    {
        for (std::size_t i = 0; i < mActors.size(); ++i)
        {
            if (mActors[i].mHandle == handle)
            {
                mActors[i] = std::move(mActors.back());
                mActors.pop_back();
                break;
            }
        }
        if (mSnapshot.mEnemyShown && mTarget == handle)
        {
            mSnapshot.mEnemyShown = false;
            mSnapshot.mEnemyDead = false;
            mTarget = {};
            mEnemyName.clear();
        }
    }

    void CombatHud::noteDamage(const DamageReport& report)
    {
        if (report.attackerIsPlayer == report.victimIsPlayer)
            return;
        const ActorHandle other = report.attackerIsPlayer ? report.victim : report.attacker;
        if (!other.valid())
            return;
        mTarget = other;
        mSince = 0.f;
        mEnemyName.clear();
        for (const Entry& entry : mActors)
            if (entry.mHandle == other)
                mEnemyName = entry.mName;
        mSnapshot.mEnemyShown = true;
        mSnapshot.mEnemyDead = false;
        mSnapshot.mEnemyHealth = clean(report.healthAfter);
        mSnapshot.mEnemyHealthMax = clean(report.healthMax);
    }

    void CombatHud::noteDeath(ActorHandle actor)
    {
        if (!mSnapshot.mEnemyShown || !actor.valid() || !(mTarget == actor))
            return;
        mSnapshot.mEnemyDead = true;
        mSnapshot.mEnemyHealth = 0.f;
        mSince = 0.f;
    }

    void CombatHud::update(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        ++mUpdates;
        if (!std::isfinite(seconds) || seconds < 0.f)
            seconds = 0.f;

        openeden::sdk::game::SubjectState player;
        if (writer.subjectState(CombatSubject::ofPlayer(), player))
        {
            mSnapshot.mPlayerHealth = clean(player.health);
            mSnapshot.mPlayerHealthMax = clean(player.healthMax);
        }
        SkyrimCombat::FighterView view;
        if (mLookup && mLookup(CombatSubject::ofPlayer(), view))
        {
            mSnapshot.mPlayerStamina = clean(view.mStamina);
            mSnapshot.mPlayerStaminaMax = clean(view.mStaminaMax);
            if (view.mMagickaMax > 0.f)
            {
                // Task 0186: magicka is a real value now; without one (no spells) the bar keeps its stand-in.
                mSnapshot.mPlayerMagicka = clean(view.mMagicka);
                mSnapshot.mPlayerMagickaMax = clean(view.mMagickaMax);
            }
        }

        if (!mSnapshot.mEnemyShown)
            return;
        mSince += seconds;
        const float limit = mSnapshot.mEnemyDead ? kHudDeadBarSeconds : kHudEnemyBarSeconds;
        if (mSince >= limit)
        {
            mSnapshot.mEnemyShown = false;
            mSnapshot.mEnemyDead = false;
            mTarget = {};
            mEnemyName.clear();
            return;
        }
        if (!mSnapshot.mEnemyDead && mLookup && mLookup(CombatSubject::of(mTarget), view))
        {
            mSnapshot.mEnemyHealth = clean(view.mHealth);
            mSnapshot.mEnemyHealthMax = clean(view.mHealthMax);
        }
    }

    void CombatHud::values(std::array<double, kHudValueCount>& out) const
    {
        out = { mSnapshot.mPlayerHealth, mSnapshot.mPlayerHealthMax, mSnapshot.mPlayerMagicka,
            mSnapshot.mPlayerMagickaMax, mSnapshot.mPlayerStamina, mSnapshot.mPlayerStaminaMax,
            mSnapshot.mEnemyShown ? 1.0 : 0.0, mSnapshot.mEnemyHealth, mSnapshot.mEnemyHealthMax,
            mSnapshot.mEnemyDead ? 1.0 : 0.0 };
    }

    // ---- HudWriter ----

    bool HudWriter::playClip(CombatSubject subject, const openeden::sdk::game::ClipRequest& r)
    {
        return mInner.playClip(subject, r);
    }
    bool HudWriter::stopClip(CombatSubject subject)
    {
        return mInner.stopClip(subject);
    }
    bool HudWriter::requestSweep(CombatSubject subject, const openeden::sdk::game::SweepRequest& r)
    {
        return mInner.requestSweep(subject, r);
    }
    bool HudWriter::reportDamage(const DamageReport& report)
    {
        const bool ok = mInner.reportDamage(report);
        if (ok)
            mHud.noteDamage(report);
        return ok;
    }
    bool HudWriter::faceHeading(CombatSubject subject, float heading)
    {
        return mInner.faceHeading(subject, heading);
    }
    bool HudWriter::clipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out)
    {
        return mInner.clipTiming(clip, perSex, out);
    }
    bool HudWriter::subjectState(CombatSubject subject, openeden::sdk::game::SubjectState& out)
    {
        return mInner.subjectState(subject, out);
    }
    bool HudWriter::launchProjectile(CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r)
    {
        return mInner.launchProjectile(owner, r);
    }
    bool HudWriter::setClipSpeed(CombatSubject subject, float multiplier)
    {
        return mInner.setClipSpeed(subject, multiplier);
    }
    bool HudWriter::clipEventTime(
        std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind, float& out)
    {
        return mInner.clipEventTime(clip, perSex, kind, out);
    }
    bool HudWriter::playSound(const openeden::sdk::game::SoundRequest& request)
    {
        return mInner.playSound(request);
    }

    bool HudWriter::reportDeath(ActorHandle actor)
    {
        const bool ok = mInner.reportDeath(actor);
        if (ok)
            mHud.noteDeath(actor);
        return ok;
    }

    // ---- the script package ----

    struct CombatHudBindings::Impl
    {
        using BindingResult = openeden::sdk::BindingResult;
        using BindingValue = openeden::sdk::BindingValue;
        using BindingType = openeden::sdk::BindingType;
        using BindingStatus = openeden::sdk::BindingStatus;

        struct ActiveHandler final : openeden::sdk::BindingHandler
        {
            const CombatHud& mHud;
            explicit ActiveHandler(const CombatHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::Bool;
                result.value.boolean = mHud.active();
                return result;
            }
        };

        struct HudHandler final : openeden::sdk::BindingHandler
        {
            const CombatHud& mHud;
            std::array<BindingValue, kHudValueCount> mElements;
            explicit HudHandler(const CombatHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                std::array<double, kHudValueCount> numbers;
                mHud.values(numbers);
                for (std::size_t i = 0; i < kHudValueCount; ++i)
                {
                    mElements[i] = BindingValue{};
                    mElements[i].type = BindingType::Double;
                    mElements[i].number = numbers[i];
                }
                BindingResult result;
                result.value.type = BindingType::Double;
                result.value.elements = mElements.data();
                result.value.count = kHudValueCount;
                return result;
            }
        };

        struct NameHandler final : openeden::sdk::BindingHandler
        {
            const CombatHud& mHud;
            explicit NameHandler(const CombatHud& hud)
                : mHud(hud)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult result;
                result.value.type = BindingType::String;
                result.value.text = mHud.enemyName();
                return result;
            }
        };

        explicit Impl(const CombatHud& hud)
            : mActive(hud)
            , mHudHandler(hud)
            , mName(hud)
        {
        }

        ActiveHandler mActive;
        HudHandler mHudHandler;
        NameHandler mName;
        std::array<openeden::sdk::FunctionDescriptor, 3> mFunctions;
    };

    CombatHudBindings::CombatHudBindings(const CombatHud& hud)
        : mImpl(std::make_unique<Impl>(hud))
    {
    }

    CombatHudBindings::~CombatHudBindings() = default;

    openeden::sdk::DeclareResult CombatHudBindings::declareTo(openeden::sdk::BindingsService& service)
    {
        using namespace openeden::sdk;
        mImpl->mFunctions[0] = FunctionDescriptor{ "active", {}, true, BindingType::Bool, BindingAccess::Read,
            &mImpl->mActive, 0 };
        mImpl->mFunctions[1] = FunctionDescriptor{ "hud", {}, true, BindingType::Double, BindingAccess::Read,
            &mImpl->mHudHandler, 0, {}, BindingShape::Array };
        mImpl->mFunctions[2] = FunctionDescriptor{ "enemy_name", {}, true, BindingType::String, BindingAccess::Read,
            &mImpl->mName, 0 };
        NamespaceDescriptor ns;
        ns.name = kHudNamespace;
        ns.version = Version{ 1, 0 };
        ns.functions = mImpl->mFunctions;
        ns.menuSafe = false;
        return service.declareNamespace(ns);
    }
}
