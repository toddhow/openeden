// Task 0292 (the College of Winterhold's tests): the spell events SkyrimCombat gives the Papyrus host (Cast, Hit,
// EffectApplied), through the combat contract with a fake data source and a recording writer (the fixture is the one
// of testskyrimsecombatmagic.cpp, copied: Firebolt and Healing as there). Written first and run red against a sink
// that was never called.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/hud.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kCasterBase = 100;
    constexpr WorldId kVictimBase = 200;
    constexpr FormId kFirebolt = 0x1000, kHealing = 0x1001, kFastHealing = 0x1002, kWard = 0x1003, kBurn = 0x1004,
                     kDrain = 0x1005, kFrost = 0x1006, kEmpty = 0x1007, kAbilityResist = 0x4000;
    constexpr FormId kFireEffect = 0x2000, kStaggerEffect = 0x2001, kDemoralizeEffect = 0x2002, kHealEffect = 0x2003,
                     kFastHealEffect = 0x2004, kAccumulateEffect = 0x2005, kPeakEffect = 0x2006, kBurnEffect = 0x2007,
                     kDrainEffect = 0x2008, kDualEffect = 0x2009, kResistEffect = 0x4001;
    constexpr FormId kProjectile = 0x3000;
    constexpr float kPi = 3.14159265358979f;

    class FakeLibrary final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId) const override { return nullptr; }
        const SpellRecord* spell(FormId id) const override { return id == kAbilityResist ? &mAbility : nullptr; }
        const MagicEffectRecord* magicEffect(FormId id) const override
        {
            return id == kResistEffect ? &mAbilityEffect : nullptr;
        }
        SpellRecord mAbility;
        MagicEffectRecord mAbilityEffect;
    };

    class FakeData final : public CombatData
    {
    public:
        FakeData()
        {
            mProj.mCommon.mId = kProjectile;
            mProj.mCommon.mEditorId = "FireboltProjectile01";
            mProj.mHasData = true;
            mProj.mType = 1;
            mProj.mSpeed = 2500.f;
            mProj.mGravity = 0.f;
            mProj.mRange = 10000.f;
            mProj.mModel = "Magic\\FireBoltProjectile.nif";

            addEffect(kFireEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile, 41);
            addEffect(kStaggerEffect, MagicArchetype::Stagger, -1, 0x8000);
            mEffects[kStaggerEffect].mConditions = 2; // gated on the effect record, as the real Firebolt's is
            addEffect(kDemoralizeEffect, MagicArchetype::Demoralize, 1, 0x409903);
            addEffect(kHealEffect, 0, 24, 0x201000);
            addEffect(kFastHealEffect, 0, 24, 0x201a00);
            addEffect(kAccumulateEffect, MagicArchetype::AccumulateMagnitude, 63, 0x40001002);
            addEffect(kPeakEffect, MagicArchetype::PeakValueModifier, 39, 0x40001002);
            addEffect(kBurnEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
            addEffect(
                kDrainEffect, MagicArchetype::Absorb, 25, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile);
            addEffect(kDualEffect, MagicArchetype::Dual, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectile);
            mEffects[kDualEffect].mSecondaryValue = 26;
            mEffects[kDualEffect].mSecondaryWeight = 0.5f;

            addSpell(kFirebolt, "Firebolt", 41, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kFireEffect, 25.f, 0, 0 }, { kStaggerEffect, 0.25f, 0, 0 }, { kDemoralizeEffect, 99.f, 15, 1 } });
            addSpell(kHealing, "Healing", 12, MagicCast::Concentration, MagicDelivery::Self,
                { { kHealEffect, 10.f, 1, 0 } });
            addSpell(kFastHealing, "FastHealing", 73, MagicCast::FireAndForget, MagicDelivery::Self,
                { { kFastHealEffect, 50.f, 0, 0 } });
            addSpell(kWard, "WardLesser", 34, MagicCast::Concentration, MagicDelivery::Self,
                { { kAccumulateEffect, 40.f, 0, 0 }, { kPeakEffect, 40.f, 0, 0 } });
            addSpell(kBurn, "Burn", 10, MagicCast::FireAndForget, MagicDelivery::Self, { { kBurnEffect, 5.f, 4, 0 } });
            addSpell(kDrain, "DrainMagicka", 20, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kDrainEffect, 10.f, 0, 0 } });
            addSpell(kFrost, "FrostDual", 10, MagicCast::FireAndForget, MagicDelivery::Aimed,
                { { kDualEffect, 10.f, 0, 0 } });
            addSpell(kEmpty, "Nothing", 5, MagicCast::FireAndForget, MagicDelivery::Self, {});
            mSpells[kFrost].mEffects[0].mEffect = kDualEffect;

            mLibrary.mAbility.mCommon.mId = kAbilityResist;
            mLibrary.mAbility.mHasData = true;
            mLibrary.mAbility.mType = SpellRecord::kTypeAbility;
            SpellEffect resist;
            resist.mEffect = kResistEffect;
            resist.mMagnitude = 50.f;
            mLibrary.mAbility.mEffects.push_back(resist);
            mLibrary.mAbilityEffect.mHasData = true;
            mLibrary.mAbilityEffect.mArchetype = 0;
            mLibrary.mAbilityEffect.mPrimaryValue = 41;

            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 100.f;
            mProfiles[kCasterBase] = caster;

            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMagickaMax = 100.f;
            mProfiles[kVictimBase] = victim;

            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100.f;
            mPlayer.mSkills.fill(15);
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            const auto it = mProfiles.find(base);
            if (it == mProfiles.end())
                return false;
            out = it->second;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const PerkLibrary* perkLibrary() override { return mUseLibrary ? &mLibrary : nullptr; }
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            for (const auto& entry : mSpells)
            {
                std::string lower = entry.second.mCommon.mEditorId;
                for (char& c : lower)
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                if (lower == editorId)
                    return &entry.second;
            }
            return nullptr;
        }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        const ProjectileRecord* projectileById(FormId id) override { return id == kProjectile ? &mProj : nullptr; }
        MagicSettings magicSettings() override { return mMagic; }

        struct E
        {
            FormId mEffect;
            float mMagnitude;
            std::uint32_t mDuration;
            std::uint32_t mConditions;
        };
        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags,
            FormId projectile = kNullFormId, std::int32_t resist = -1)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            m.mResistValue = resist;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t cast, std::uint32_t delivery,
            const std::vector<E>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = cast;
            s.mDelivery = delivery;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                se.mDuration = e.mDuration;
                se.mConditions = e.mConditions;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        ProjectileRecord mProj;
        FakeLibrary mLibrary;
        bool mUseLibrary = false;
        MagicSettings mMagic;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class FakeWriter final : public CombatRequestWriter
    {
    public:
        struct Clip
        {
            CombatSubject mSubject;
            ClipSlot mSlot;
            std::string mName;
            std::uint32_t mToken;
        };
        struct Launch
        {
            CombatSubject mOwner;
            ProjectileRequest mRequest;
            std::string mModel;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.slot, std::string(request.clip), request.token });
            return true;
        }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            out = 0.f;
            if (kind != ClipEventKind::SpellFire)
                return false;
            if (clip == "MRh_Release")
                out = 0.167f;
            else if (clip == "MRh_SelfRelease")
                out = 0.367f;
            else
                return false;
            return true;
        }
        bool launchProjectile(CombatSubject owner, const ProjectileRequest& request) override
        {
            mLaunches.push_back({ owner, request, std::string(request.model) });
            mLaunches.back().mRequest.model = mLaunches.back().mModel;
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override
        {
            ++mSweeps;
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle actor) override
        {
            mDeaths.push_back(actor);
            return true;
        }
        bool faceHeading(CombatSubject, float) override { return true; }
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (subject.player)
            {
                out = mPlayerState;
                return true;
            }
            const auto it = mStates.find(subject.actor.reference);
            if (it == mStates.end())
                return false;
            out = it->second;
            return true;
        }

        std::vector<Clip> mClips;
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        int mSweeps = 0;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    // "Archer" (handle 10) at the origin facing +X; "Brenuin" (handle 20) and the player 800 units east.
    struct SpellEvents : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start()
        {
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(10, kCasterBase, "Archer"));
            mCombat.onActorAdded(info(20, kVictimBase, "Brenuin"));
            SubjectState archer;
            archer.heading = kPi / 2.f;
            mWriter.mStates[10] = archer;
            SubjectState victim = archer;
            victim.x = 800.0;
            mWriter.mStates[20] = victim;
            mWriter.mPlayerState = victim;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        bool traceHas(const std::string& text) const
        {
            return std::any_of(mTrace.begin(), mTrace.end(),
                [&](const std::string& line) { return line.find(text) != std::string::npos; });
        }
        SkyrimCombat::FighterView view(CombatSubject subject)
        {
            SkyrimCombat::FighterView v;
            EXPECT_TRUE(mCombat.fighterView(subject, v));
            return v;
        }
        SkyrimCombat::FighterView viewB() { return view(CombatSubject::of(mB)); }
        SkyrimCombat::FighterView viewA() { return view(CombatSubject::of(mA)); }
        SkyrimCombat::FighterView viewP() { return view(CombatSubject::ofPlayer()); }

        // The player casts Firebolt (press edge) and the shot is launched; returns its token.
        std::uint32_t playerFirebolt()
        {
            EXPECT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
            mCombat.onPlayerAttackEdge(true);
            update();
            update();
            update();
            EXPECT_FALSE(mWriter.mLaunches.empty());
            return mWriter.mLaunches.empty() ? 0 : mWriter.mLaunches.back().mRequest.token;
        }
        void hit(std::uint32_t token, ActorHandle victim)
        {
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = token;
            r.fate = ProjectileFate::HitActor;
            r.actor = victim;
            mCombat.onProjectileResult(r);
        }
    };

    using Event = SkyrimCombat::SpellEvent;

    // The fixture's events, in the order they arrived.
    struct Recorder
    {
        std::vector<Event> mEvents;
        void attach(SkyrimCombat& combat)
        {
            combat.setSpellEventSink([this](const Event& e) { mEvents.push_back(e); });
        }
        std::size_t count(Event::Kind kind) const
        {
            return static_cast<std::size_t>(
                std::count_if(mEvents.begin(), mEvents.end(), [&](const Event& e) { return e.mKind == kind; }));
        }
        const Event* first(Event::Kind kind) const
        {
            for (const Event& e : mEvents)
                if (e.mKind == kind)
                    return &e;
            return nullptr;
        }
    };

    TEST_F(SpellEvents, aFireboltCastGivesOneCastEventNamingTheSpellAndTheCaster)
    {
        Recorder rec;
        rec.attach(mCombat);
        start();
        playerFirebolt();
        ASSERT_EQ(rec.count(Event::Kind::Cast), 1u);
        const Event* cast = rec.first(Event::Kind::Cast);
        EXPECT_TRUE(cast->mCaster.player);
        EXPECT_EQ(cast->mSpell, kFirebolt);
        EXPECT_FALSE(cast->mHasTarget);
        EXPECT_EQ(rec.count(Event::Kind::Hit), 0u);
    }

    TEST_F(SpellEvents, aFireboltThatHitsAnActorGivesHitThenTheEffectsThatReachedIt)
    {
        Recorder rec;
        rec.attach(mCombat);
        start();
        hit(playerFirebolt(), mB);
        update();
        ASSERT_EQ(rec.count(Event::Kind::Hit), 1u);
        const Event* h = rec.first(Event::Kind::Hit);
        EXPECT_TRUE(h->mCaster.player);
        EXPECT_TRUE(h->mHasTarget);
        EXPECT_FALSE(h->mTarget.player);
        EXPECT_EQ(h->mTarget.actor.reference, mB.reference);
        EXPECT_EQ(h->mSpell, kFirebolt);
        // The Stagger effect carries a condition the combat cannot meet, so only the damage and the Demoralize effect
        // that is not gated reach the target... the test pins what this build does: the gated one is not announced.
        std::vector<FormId> applied;
        for (const Event& e : rec.mEvents)
            if (e.mKind == Event::Kind::EffectApplied)
            {
                EXPECT_EQ(e.mSpell, kFirebolt);
                EXPECT_TRUE(e.mHasTarget);
                EXPECT_EQ(e.mTarget.actor.reference, mB.reference);
                applied.push_back(e.mEffect);
            }
        EXPECT_NE(std::find(applied.begin(), applied.end(), kFireEffect), applied.end());
        EXPECT_EQ(std::find(applied.begin(), applied.end(), kStaggerEffect), applied.end());
        // order: the hit comes before its effects
        std::size_t hitAt = 0, firstEffectAt = 0;
        for (std::size_t i = 0; i < rec.mEvents.size(); ++i)
        {
            if (rec.mEvents[i].mKind == Event::Kind::Hit)
                hitAt = i;
            if (rec.mEvents[i].mKind == Event::Kind::EffectApplied && firstEffectAt == 0)
                firstEffectAt = i;
        }
        EXPECT_LT(hitAt, firstEffectAt);
    }

    TEST_F(SpellEvents, aFireboltThatHitsTheWorldGivesAHitWithNoTargetAtTheEndPoint)
    {
        Recorder rec;
        rec.attach(mCombat);
        start();
        const std::uint32_t token = playerFirebolt();
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = token;
        r.fate = ProjectileFate::HitWorld;
        r.x = 12.5;
        r.y = -3.25;
        r.z = 400.0;
        mCombat.onProjectileResult(r);
        update();
        ASSERT_EQ(rec.count(Event::Kind::Hit), 1u);
        const Event* h = rec.first(Event::Kind::Hit);
        EXPECT_FALSE(h->mHasTarget);
        EXPECT_EQ(h->mSpell, kFirebolt);
        EXPECT_DOUBLE_EQ(h->mX, 12.5);
        EXPECT_DOUBLE_EQ(h->mY, -3.25);
        EXPECT_DOUBLE_EQ(h->mZ, 400.0);
        EXPECT_EQ(rec.count(Event::Kind::EffectApplied), 0u);
    }

    TEST_F(SpellEvents, aSelfSpellAnnouncesItsEffectOnTheCasterAndNoHit)
    {
        Recorder rec;
        rec.attach(mCombat);
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("FastHealing"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        ASSERT_EQ(rec.count(Event::Kind::Cast), 1u);
        EXPECT_EQ(rec.count(Event::Kind::Hit), 0u);
        ASSERT_EQ(rec.count(Event::Kind::EffectApplied), 1u);
        const Event* e = rec.first(Event::Kind::EffectApplied);
        EXPECT_EQ(e->mEffect, kFastHealEffect);
        EXPECT_TRUE(e->mCaster.player);
        EXPECT_TRUE(e->mHasTarget);
        EXPECT_TRUE(e->mTarget.player);
    }

    TEST_F(SpellEvents, aSpellThatRunsOutOfMagickaGivesNoEvent)
    {
        Recorder rec;
        rec.attach(mCombat);
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        for (int i = 0; i < 4; ++i) // 100 magicka: two bolts of 41 and no third
        {
            mCombat.onPlayerAttackEdge(true);
            updates(4);
        }
        EXPECT_LE(rec.count(Event::Kind::Cast), 2u);
    }

    TEST_F(SpellEvents, withNoSinkNothingChanges)
    {
        start();
        hit(playerFirebolt(), mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 25.f);
    }
}
