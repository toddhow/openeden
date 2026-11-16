// Task 0186 (milestone M-S5, stage S5-8): the Skyrim pack's magic runtime (game_skyrimse/combat.hpp, magic.hpp), tested
// through the combat contract 0.4 with a fake data source and a recording writer. Every number is derived by hand:
//  - Firebolt: cost 41, fire and forget, aimed, one damage effect of 25 on health (fire resistance 41) with a
//    projectile of speed 2500, gravity 0, range 10000, a Stagger effect and a Demoralize effect of 15 seconds that carry
//    conditions (so they are skipped unless the test setting says otherwise);
//  - Healing: cost 12 a second, concentration, self, 10 health a second;
//  - FastHealing: cost 73, self, 50 health at once;
//  - WardLesser: cost 34 a second, concentration, self: Accumulate 40 (the charge a second) and Peak 40 (the ward's
//    strength); the ward holds 40 x 1.3 = 52 at most.
// The victim has 50 health and no armour, the caster 100 magicka. HOW the numbers combine is RECALLED-VERIFY: these
// tests pin THIS implementation.
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
    struct CombatMagic : ::testing::Test
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

    ClipEvent spellFire(CombatSubject subject, std::uint32_t token)
    {
        ClipEvent e;
        e.subject = subject;
        e.slot = ClipSlot::Cast;
        e.token = token;
        e.kind = ClipEventKind::SpellFire;
        return e;
    }

    // ---- Firebolt --------------------------------------------------------------------------------------------------

    TEST_F(CombatMagic, aPlayerFireboltSpendsItsCostAndLaunchesTheProjectileOfItsEffect)
    {
        start();
        const std::uint32_t token = playerFirebolt();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        const ProjectileRequest& r = mWriter.mLaunches[0].mRequest;
        EXPECT_FLOAT_EQ(r.speed, 2500.f);
        EXPECT_FLOAT_EQ(r.gravity, 0.f);
        EXPECT_FLOAT_EQ(r.range, 10000.f);
        EXPECT_NE(mWriter.mLaunches[0].mModel.find("FireBoltProjectile"), std::string::npos);
        EXPECT_TRUE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_NE(token, 0u);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 59.f); // 100 - 41
        EXPECT_FLOAT_EQ(viewP().mMagickaMax, 100.f);
        EXPECT_EQ(mCombat.spellsCast(), 1u);
        EXPECT_EQ(mCombat.spellShotsRemembered(), 1u);
    }

    TEST_F(CombatMagic, theAttackControlCastsInsteadOfSwinging)
    {
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        mCombat.onPlayerAttack(false);
        updates(4);
        EXPECT_EQ(mWriter.mSweeps, 0);
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(CombatMagic, aFireboltThatHitsTakes25OffTheVictimsHealth)
    {
        start();
        const std::uint32_t token = playerFirebolt();
        hit(token, mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 25.f); // 50 - 25
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_FLOAT_EQ(mWriter.mDamage.back().amount, 25.f);
        EXPECT_FLOAT_EQ(mWriter.mDamage.back().healthAfter, 25.f);
        EXPECT_TRUE(mWriter.mDamage.back().attackerIsPlayer);
        EXPECT_FALSE(mWriter.mDamage.back().victimIsPlayer);
        EXPECT_EQ(mCombat.spellShotsRemembered(), 0u);
        // The perk gated Stagger and Demoralize effects are skipped: no clip, nothing running.
        EXPECT_TRUE(mWriter.mClips.empty());
        EXPECT_EQ(mCombat.activeEffects(), 0u);
        EXPECT_FALSE(viewB().mDemoralized);
    }

    TEST_F(CombatMagic, twoFireboltsKillAFiftyHealthVictimAndItDies)
    {
        start();
        for (int i = 0; i < 2; ++i)
        {
            hit(playerFirebolt(), mB);
            update();
        }
        EXPECT_FLOAT_EQ(viewB().mHealth, 0.f);
        EXPECT_TRUE(viewB().mDead);
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths[0], mB);
    }

    TEST_F(CombatMagic, aFireboltThatMissesDoesNothingToAnyone)
    {
        start();
        const std::uint32_t token = playerFirebolt();
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = token;
        r.fate = ProjectileFate::HitWorld;
        mCombat.onProjectileResult(r);
        update();
        SkyrimCombat::FighterView victim;
        if (mCombat.fighterView(CombatSubject::of(mB), victim))
        {
            EXPECT_FLOAT_EQ(victim.mHealth, 50.f);
        }
        EXPECT_TRUE(mWriter.mDamage.empty());
        EXPECT_EQ(mCombat.spellShotsRemembered(), 0u);
    }

    TEST_F(CombatMagic, aFireResistanceOf50PercentHalvesTheDamage)
    {
        mData.mUseLibrary = true;
        mData.mProfiles[kVictimBase].mSpells = { kAbilityResist };
        start();
        hit(playerFirebolt(), mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 37.5f); // 50 - 25 x 0.5
    }

    TEST_F(CombatMagic, withTheConditionSettingTheStaggerAndDemoralizeEffectsApplyAndTheLatterEndsOnTime)
    {
        mCombat.setTestSpellConditions(true);
        start();
        hit(playerFirebolt(), mB);
        update(); // the hit is resolved here: the stagger clip plays and the demoralize effect starts
        EXPECT_FLOAT_EQ(viewB().mHealth, 25.f);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Stagger);
        EXPECT_TRUE(viewB().mStaggered);
        EXPECT_EQ(mCombat.activeEffects(), 1u);
        EXPECT_TRUE(viewB().mDemoralized);
        updates(29, 0.5f); // 14.5 seconds
        EXPECT_EQ(mCombat.activeEffects(), 1u);
        EXPECT_EQ(mCombat.effectsEnded(), 0u);
        update(0.5f); // 15 seconds: it ends
        EXPECT_EQ(mCombat.activeEffects(), 0u);
        EXPECT_EQ(mCombat.effectsEnded(), 1u);
        EXPECT_FALSE(viewB().mDemoralized);
    }

    // ---- the cost --------------------------------------------------------------------------------------------------

    TEST_F(CombatMagic, aCastWithTooLittleMagickaDoesNotHappen)
    {
        mData.mPlayer.mMagickaMax = 30.f;
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_FLOAT_EQ(viewP().mMagicka, 30.f);
        EXPECT_EQ(mCombat.spellsCast(), 0u);
        EXPECT_TRUE(traceHas("magicka"));
    }

    TEST_F(CombatMagic, magickaRegeneratesAfterTheDelayAtTheRacesRate)
    {
        mData.mPlayer.mMagickaRegenPercent = 3.f; // 3 percent of 100 a second
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("FastHealing"));
        mCombat.onPlayerAttackEdge(true);
        updates(4); // cast at 0.25, fires at the update after 0.25 + 0.367: 73 spent
        const float afterCast = viewP().mMagicka;
        EXPECT_FLOAT_EQ(afterCast, 27.f);
        updates(40); // 10 seconds more: 0.5 s of delay, then 3 a second (not in combat)
        const float regained = viewP().mMagicka - afterCast;
        EXPECT_NEAR(regained, 3.f * 9.75f, 1.0f);
        EXPECT_LE(viewP().mMagicka, 100.f);
    }

    // ---- healing ---------------------------------------------------------------------------------------------------

    TEST_F(CombatMagic, aHeldHealingRestores10HealthASecondAndCosts12MagickaASecond)
    {
        start();
        mWriter.mPlayerState.health = 40.f;
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        mCombat.onPlayerAttackEdge(true);
        updates(8); // fires when the clock passes 0.25 + 0.367, then ticks
        mCombat.onPlayerAttackEdge(false);
        update();
        const float healed = mWriter.mPlayerState.health - 40.f;
        const float spent = 100.f - viewP().mMagicka;
        EXPECT_GT(healed, 10.f);
        EXPECT_NEAR(healed / 10.f, spent / 12.f, 1.0e-3f); // the same seconds paid for what was restored
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        // Nothing more happens after the release.
        const float health = mWriter.mPlayerState.health;
        updates(4);
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, health);
        EXPECT_FLOAT_EQ(100.f - viewP().mMagicka, spent);
    }

    TEST_F(CombatMagic, aHeldSpellThatIsReleasedBeforeItGoesNeverStartsAndCostsNothing)
    {
        start();
        mWriter.mPlayerState.health = 40.f;
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        mCombat.onPlayerAttackEdge(true);
        update(); // the cast begins; the spell would go 0.367 seconds later
        mCombat.onPlayerAttackEdge(false);
        updates(8);
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        EXPECT_EQ(mCombat.castsWaiting(), 0u);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 100.f);
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 40.f);
    }

    TEST_F(CombatMagic, aFireAndForgetSpellStillGoesWhenTheControlIsReleasedAtOnce)
    {
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        mCombat.onPlayerAttackEdge(true);
        update();
        mCombat.onPlayerAttackEdge(false);
        updates(4);
        EXPECT_EQ(mWriter.mLaunches.size(), 1u); // a tap fires it
    }

    TEST_F(CombatMagic, healingNeverRaisesHealthAboveTheMaximum)
    {
        start();
        mWriter.mPlayerState.health = 98.f;
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        mCombat.onPlayerAttackEdge(true);
        updates(10);
        mCombat.onPlayerAttackEdge(false);
        update();
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 100.f);
    }

    TEST_F(CombatMagic, aHeldSpellEndsWhenTheMagickaRunsOut)
    {
        mData.mPlayer.mMagickaMax = 6.f;
        start();
        mWriter.mPlayerState.health = 40.f;
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Healing"));
        mCombat.onPlayerAttackEdge(true);
        updates(12);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 0.f);
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        EXPECT_NEAR(mWriter.mPlayerState.health - 40.f, 5.f, 1.0e-3f); // 6 magicka is half a second of Healing
        EXPECT_TRUE(traceHas("out of magicka"));
    }

    TEST_F(CombatMagic, fastHealingCappedAtTheMaximumAndPaidInFull)
    {
        start();
        const std::uint32_t token = playerFirebolt();
        hit(token, mB);
        update();
        ASSERT_FLOAT_EQ(viewB().mHealth, 25.f);
        mCombat.setTestCasts("Brenuin:FastHealing");
        mCombat.setTestCastEvery(1.f);
        updates(6);
        ASSERT_FALSE(mWriter.mClips.empty());
        const auto cast = std::find_if(mWriter.mClips.begin(), mWriter.mClips.end(),
            [](const FakeWriter::Clip& c) { return c.mSlot == ClipSlot::Cast; });
        ASSERT_NE(cast, mWriter.mClips.end());
        EXPECT_EQ(cast->mName, "MRh_SelfRelease"); // a self spell uses the self clip
        mCombat.onClipEvent(spellFire(CombatSubject::of(mB), cast->mToken));
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 50.f); // 25 + 50, capped at 50
        EXPECT_FLOAT_EQ(viewB().mMagicka, 27.f); // 100 - 73
    }

    // ---- the ward --------------------------------------------------------------------------------------------------

    // Brenuin wards herself and the ward has charged to its maximum (52).
    void chargeWard(CombatMagic& t)
    {
        t.start();
        ASSERT_TRUE(t.mCombat.setTestCasts("Brenuin:WardLesser"));
        t.mCombat.setTestCastEvery(1.f);
        t.mCombat.setTestCastHold(10.f);
        t.updates(8);
        const auto cast = std::find_if(t.mWriter.mClips.begin(), t.mWriter.mClips.end(),
            [](const FakeWriter::Clip& c) { return c.mSlot == ClipSlot::Cast; });
        ASSERT_NE(cast, t.mWriter.mClips.end());
        t.mCombat.onClipEvent(spellFire(CombatSubject::of(t.mB), cast->mToken));
        t.updates(8); // 2 seconds of charging at 40 a second: full
    }

    TEST_F(CombatMagic, aWardChargesToItsMaximumAndDropsWhenTheCastEnds)
    {
        chargeWard(*this);
        EXPECT_FLOAT_EQ(viewB().mWardPower, 52.f);
        EXPECT_EQ(mCombat.concentrationsHeld(), 1u);
        updates(40); // past the 10 second hold
        EXPECT_FLOAT_EQ(viewB().mWardPower, 0.f);
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
    }

    TEST_F(CombatMagic, aWardBlocksAFireboltAndTakesItsMagnitudeFromItsPower)
    {
        chargeWard(*this);
        const std::uint32_t token = playerFirebolt();
        hit(token, mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 50.f); // blocked
        EXPECT_FLOAT_EQ(viewB().mWardPower, 27.f); // 52 - 25
        EXPECT_TRUE(traceHas("ward"));
    }

    TEST_F(CombatMagic, aWardThatRunsOutLetsTheRestOfTheMagnitudeThrough)
    {
        mData.mPlayer.mMagickaMax = 1000.f;
        mData.mProfiles[kVictimBase].mMagickaMax = 1000.f; // the ward costs 34 a second while it is held
        chargeWard(*this);
        mData.mSpells[kWard].mEffects[0].mMagnitude = 0.f; // the ward stops charging: only the bolts change it
        mWriter.mPlayerState.health = 100.f;
        for (int i = 0; i < 3; ++i)
        {
            hit(playerFirebolt(), mB);
            update();
            mCombat.onPlayerAttackEdge(false);
        }
        // 52 - 25 - 25 = 2; the third bolt: 2 absorbed, 23 through.
        EXPECT_FLOAT_EQ(viewB().mWardPower, 0.f);
        EXPECT_FLOAT_EQ(viewB().mHealth, 27.f);
    }

    // ---- effects over time, absorb, dual --------------------------------------------------------------------------

    TEST_F(CombatMagic, aBurnForFourSecondsTakes5ASecondAndEndsOnTime)
    {
        start();
        mCombat.setTestCasts("Brenuin:Burn");
        mCombat.setTestCastEvery(1.f);
        updates(5);
        const auto cast = std::find_if(mWriter.mClips.begin(), mWriter.mClips.end(),
            [](const FakeWriter::Clip& c) { return c.mSlot == ClipSlot::Cast; });
        ASSERT_NE(cast, mWriter.mClips.end());
        mCombat.setTestCasts(""); // one cast only
        mCombat.onClipEvent(spellFire(CombatSubject::of(mB), cast->mToken));
        update(0.5f); // fires; the effect starts
        EXPECT_EQ(mCombat.activeEffects(), 1u);
        updates(7, 0.5f); // 3.5 seconds
        EXPECT_NEAR(viewB().mHealth, 50.f - 5.f * 3.5f, 1.0e-3f);
        EXPECT_EQ(mCombat.activeEffects(), 1u);
        update(0.5f); // 4 seconds: done
        EXPECT_NEAR(viewB().mHealth, 30.f, 1.0e-3f);
        EXPECT_EQ(mCombat.activeEffects(), 0u);
        EXPECT_EQ(mCombat.effectsEnded(), 1u);
        updates(4, 0.5f);
        EXPECT_NEAR(viewB().mHealth, 30.f, 1.0e-3f);
    }

    TEST_F(CombatMagic, anAbsorbEffectMovesMagickaFromTheVictimToTheCaster)
    {
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("DrainMagicka"));
        mCombat.onPlayerAttackEdge(true);
        updates(3);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 80.f); // 100 - 20
        hit(mWriter.mLaunches[0].mRequest.token, mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mMagicka, 90.f); // 100 - 10
        EXPECT_FLOAT_EQ(viewP().mMagicka, 90.f); // 80 + 10
    }

    TEST_F(CombatMagic, aDualEffectChangesTheSecondValueByItsWeight)
    {
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("FrostDual"));
        mCombat.onPlayerAttackEdge(true);
        updates(3);
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        hit(mWriter.mLaunches[0].mRequest.token, mB);
        update();
        EXPECT_FLOAT_EQ(viewB().mHealth, 40.f); // 50 - 10
        EXPECT_FLOAT_EQ(viewB().mStamina, 75.f); // 80 - 10 x 0.5
    }

    // ---- NPC casters -----------------------------------------------------------------------------------------------

    TEST_F(CombatMagic, anNpcCastsAnAimedSpellThroughItsClipAndTheSpellFireEvent)
    {
        start();
        ASSERT_TRUE(mCombat.setTestCasts("Archer:Firebolt"));
        mCombat.setTestCastEvery(2.f);
        updates(7); // 1.75 s: nothing yet
        EXPECT_TRUE(mWriter.mClips.empty());
        update(); // 2.0 s: the cast begins
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        EXPECT_EQ(mWriter.mClips[0].mSlot, ClipSlot::Cast);
        EXPECT_EQ(mWriter.mClips[0].mName, "MRh_Release");
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_FLOAT_EQ(viewA().mMagicka, 100.f); // paid when the spell goes
        mCombat.onClipEvent(spellFire(CombatSubject::of(mA), mWriter.mClips[0].mToken));
        update();
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FALSE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_FLOAT_EQ(viewA().mMagicka, 59.f);
        // The bolt flies at the player, who is 800 units east: heading about +X (1.5708).
        EXPECT_NEAR(mWriter.mLaunches[0].mRequest.heading, kPi / 2.f, 0.01f);
        // And hits the player: 100 - 25.
        ProjectileResult r;
        r.owner = CombatSubject::of(mA);
        r.token = mWriter.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitPlayer;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 75.f);
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_FALSE(mWriter.mDamage.back().attackerIsPlayer);
        EXPECT_TRUE(mWriter.mDamage.back().victimIsPlayer);
    }

    TEST_F(CombatMagic, aCastWhoseClipIsInterruptedCostsNothingAndLaunchesNothing)
    {
        start();
        ASSERT_TRUE(mCombat.setTestCasts("Archer:Firebolt"));
        mCombat.setTestCastEvery(1.f);
        updates(4);
        ASSERT_EQ(mWriter.mClips.size(), 1u);
        mCombat.onClipFinished(CombatSubject::of(mA), mWriter.mClips[0].mToken, ClipEnd::Interrupted);
        mCombat.onClipEvent(spellFire(CombatSubject::of(mA), mWriter.mClips[0].mToken)); // a late event is ignored
        update();
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_FLOAT_EQ(viewA().mMagicka, 100.f);
        EXPECT_EQ(mCombat.castsWaiting(), 0u);
    }

    TEST_F(CombatMagic, aCastThatNeverGetsItsEventGivesUp)
    {
        start();
        ASSERT_TRUE(mCombat.setTestCasts("Archer:Firebolt"));
        mCombat.setTestCastEvery(1.f);
        updates(4);
        mCombat.setTestCasts("");
        EXPECT_EQ(mCombat.castsWaiting(), 1u);
        updates(20, 0.25f); // more than kCastEventTimeoutSeconds
        EXPECT_EQ(mCombat.castsWaiting(), 0u);
        EXPECT_TRUE(mWriter.mLaunches.empty());
    }

    TEST_F(CombatMagic, anActorThatGoesTakesItsCastsAndEffectsWithIt)
    {
        mCombat.setTestSpellConditions(true);
        start();
        hit(playerFirebolt(), mB);
        update();
        ASSERT_EQ(mCombat.activeEffects(), 1u);
        mCombat.onActorRemoved(mB);
        EXPECT_EQ(mCombat.activeEffects(), 0u);
        update();
        update();
    }

    TEST_F(CombatMagic, badSettingsAreRefusedOrIgnored)
    {
        start();
        EXPECT_FALSE(mCombat.setTestCasts("NoColon"));
        EXPECT_FALSE(mCombat.setTestCasts(std::string(5000, 'a')));
        EXPECT_FALSE(mCombat.setTestPlayerSpell(std::string(500, 'a')));
        EXPECT_TRUE(mCombat.setTestPlayerSpell("DoesNotExist"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("NOT found"));
        mCombat.setTestCastEvery(std::nanf(""));
        mCombat.setTestCastHold(-1.f);
        mCombat.setTestPlayerCastEvery(std::nanf(""));
        updates(2);
    }

    TEST_F(CombatMagic, aSpellWithNoEffectsDoesNothingAndSpendsNothing)
    {
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Nothing"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatMagic, anAutomaticPlayerCastRepeatsEveryPeriod)
    {
        mData.mPlayer.mMagickaMax = 1000.f;
        start();
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        mCombat.setTestPlayerCastEvery(2.f);
        updates(40); // 10 seconds
        EXPECT_GE(mWriter.mLaunches.size(), 4u); // presses at 1, 3, 5, 7 and 9 seconds
        EXPECT_LE(mWriter.mLaunches.size(), 5u);
    }

    // ---- repeatability ---------------------------------------------------------------------------------------------

    std::vector<std::string> runScenario()
    {
        FakeData data;
        SkyrimCombat combat;
        FakeWriter writer;
        std::vector<std::string> trace;
        combat.setTrace([&](const std::string& line) { trace.push_back(line); });
        combat.setData(&data);
        combat.onActorAdded(info(10, kCasterBase, "Archer"));
        combat.onActorAdded(info(20, kVictimBase, "Brenuin"));
        SubjectState s;
        s.heading = kPi / 2.f;
        writer.mStates[10] = s;
        s.x = 800.0;
        writer.mStates[20] = s;
        writer.mPlayerState = s;
        writer.mPlayerState.healthMax = 100.f;
        writer.mPlayerState.health = 40.f;
        combat.setTestPlayerSpell("Healing");
        combat.setTestCasts("Brenuin:WardLesser,Archer:Firebolt");
        combat.setTestCastEvery(1.f);
        combat.setTestCastHold(2.f);
        combat.onPlayerAttackEdge(true);
        for (int i = 0; i < 80; ++i)
        {
            combat.combatUpdate(1.f / 60.f, writer);
            if (i == 50)
                combat.onPlayerAttackEdge(false);
            for (const FakeWriter::Clip& c : writer.mClips)
                if (c.mSlot == ClipSlot::Cast && c.mToken != 0)
                    combat.onClipEvent(spellFire(c.mSubject, c.mToken));
            writer.mClips.clear();
        }
        trace.push_back("health " + std::to_string(writer.mPlayerState.health));
        return trace;
    }

    TEST(CombatMagicDeterminism, twoRunsOfTheSameScenarioGiveTheSameTrace)
    {
        const std::vector<std::string> a = runScenario();
        const std::vector<std::string> b = runScenario();
        EXPECT_GT(a.size(), 5u);
        EXPECT_EQ(a, b);
    }

    // ---- the HUD ---------------------------------------------------------------------------------------------------

    TEST_F(CombatMagic, theHudShowsTheRealMagickaOfThePlayerAfterACast)
    {
        start();
        CombatHud hud;
        hud.setFighterLookup(
            [this](CombatSubject s, SkyrimCombat::FighterView& out) { return mCombat.fighterView(s, out); });
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Firebolt"));
        mCombat.onPlayerAttackEdge(true);
        for (int i = 0; i < 3; ++i)
        {
            mCombat.combatUpdate(0.25f, mWriter);
            hud.update(0.25f, mWriter);
        }
        EXPECT_FLOAT_EQ(hud.snapshot().mPlayerMagicka, 59.f);
        EXPECT_FLOAT_EQ(hud.snapshot().mPlayerMagickaMax, 100.f);
    }

    TEST_F(CombatMagic, theHudKeepsItsStandInWhenThePlayerHasNoMagicka)
    {
        mData.mPlayer.mMagickaMax = 0.f;
        start();
        CombatHud hud;
        hud.setFighterLookup(
            [this](CombatSubject s, SkyrimCombat::FighterView& out) { return mCombat.fighterView(s, out); });
        mCombat.combatUpdate(0.25f, mWriter);
        hud.update(0.25f, mWriter);
        EXPECT_FLOAT_EQ(hud.snapshot().mPlayerMagicka, kHudMagickaStandIn);
        EXPECT_FLOAT_EQ(hud.snapshot().mPlayerMagickaMax, kHudMagickaStandIn);
    }

    // ---- the contract and the HUD view ----------------------------------------------------------------------------

    TEST(CombatMagicContract, theCastClipsAreInTheMagicTable)
    {
        const SlotTable table = SlotTable::magic();
        EXPECT_EQ(table.clipFor(ClipSlot::Cast, kMagicAimed), "MRh_Release");
        EXPECT_EQ(table.clipFor(ClipSlot::Cast, kMagicSelf), "MRh_SelfRelease");
        EXPECT_TRUE(table.clipFor(ClipSlot::Attack, kMagicAimed).empty());
        EXPECT_EQ(SlotTable::humanoid().size(), 7u); // the melee table is untouched
    }

    TEST(CombatMagicContract, theContractIsZeroPointFourAndHasASpellFireKind)
    {
        EXPECT_EQ(kCombatRulesVersion.major, 0);
        EXPECT_EQ(kCombatRulesVersion.minor, 4);
        EXPECT_EQ(static_cast<int>(ClipEventKind::SpellFire), 16);
    }
}
