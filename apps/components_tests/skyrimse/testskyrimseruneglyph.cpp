// Task 0262: a rune's glyph is taken away when the rune goes off or ends. The pack asks the core to remove the stuck
// projectile with CombatRequestWriter::cancelProjectile(token); every wrapper writer must forward the call.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/hud.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/papyrus_world.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using namespace game_skyrimse::crime;

    constexpr WorldId kCasterBase = 100, kEnemyBase = 200;
    constexpr FormId kFireRune = 0x1002, kRuneEffect = 0x2003, kRuneProj = 0x3002, kRuneExpl = 0x4000;
    constexpr float kPi = 3.14159265358979f;

    // The player casts FireRune (target location, cost 20; its projectile names an explosion of 320 units and a
    // trigger distance of 150). One enemy, Brenuin (20), 50 health.
    class RuneData final : public CombatData
    {
    public:
        RuneData()
        {
            ProjectileRecord p;
            p.mCommon.mId = kRuneProj;
            p.mCommon.mEditorId = "RuneProj";
            p.mHasData = true;
            p.mType = 1;
            p.mSpeed = 1000.f;
            p.mRange = 10000.f;
            p.mModel = "Magic\\RuneFire.nif";
            p.mExplosion = kRuneExpl;
            p.mProximity = 150.f;
            mProjectile = p;
            mExplosion.mCommon.mId = kRuneExpl;
            mExplosion.mHasData = true;
            mExplosion.mRadius = 320.f;

            mEffect.mCommon.mId = kRuneEffect;
            mEffect.mCommon.mEditorId = "RuneEffect";
            mEffect.mHasData = true;
            mEffect.mArchetype = 0;
            mEffect.mPrimaryValue = 24;
            mEffect.mFlags = MagicFlag::Hostile | MagicFlag::Detrimental;
            mEffect.mProjectile = kRuneProj;
            mEffect.mDelivery = MagicDelivery::TargetLocation;

            mSpell.mCommon.mId = kFireRune;
            mSpell.mCommon.mEditorId = "FireRune";
            mSpell.mHasData = true;
            mSpell.mCost = 20;
            mSpell.mCastType = MagicCast::FireAndForget;
            mSpell.mDelivery = MagicDelivery::TargetLocation;
            SpellEffect effect;
            effect.mEffect = kRuneEffect;
            effect.mMagnitude = 20.f;
            effect.mArea = 0;
            mSpell.mEffects.push_back(effect);

            FighterProfile caster;
            caster.mHealthMax = 100.f;
            caster.mStaminaMax = 100.f;
            caster.mMagickaMax = 100.f;
            mProfiles[kCasterBase] = caster;
            FighterProfile victim;
            victim.mHealthMax = 50.f;
            victim.mStaminaMax = 80.f;
            victim.mMagickaMax = 100.f;
            victim.mAi.mRelationToPlayer = 1; // enemy
            mProfiles[kEnemyBase] = victim;
            mPlayer.mHealthMax = 100.f;
            mPlayer.mStaminaMax = 100.f;
            mPlayer.mMagickaMax = 100000.f; // enough for every cast of the table test
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
        const SpellRecord* spellByEditorId(std::string_view editorId) override
        {
            return editorId == "firerune" ? &mSpell : nullptr;
        }
        const SpellRecord* spellById(FormId id) override { return id == kFireRune ? &mSpell : nullptr; }
        const MagicEffectRecord* effectById(FormId id) override { return id == kRuneEffect ? &mEffect : nullptr; }
        const ProjectileRecord* projectileById(FormId id) override { return id == kRuneProj ? &mProjectile : nullptr; }
        const ExplosionRecord* explosionById(FormId id) override { return id == kRuneExpl ? &mExplosion : nullptr; }
        MagicSettings magicSettings() override { return {}; }

        SpellRecord mSpell;
        MagicEffectRecord mEffect;
        ProjectileRecord mProjectile;
        ExplosionRecord mExplosion;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class RuneWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
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
        bool launchProjectile(CombatSubject, const ProjectileRequest& request) override
        {
            mLaunched.push_back(request.token);
            return true;
        }
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
        bool reportDamage(const DamageReport& report) override
        {
            if (report.victimIsPlayer)
                mPlayerState.health = report.healthAfter;
            return true;
        }
        bool reportDeath(ActorHandle) override { return true; }
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
        bool castRay(CombatSubject, const RayRequest&, RayHit& out) override
        {
            out = {};
            return true;
        }
        bool cancelProjectile(std::uint32_t token) override
        {
            mCancelled.push_back(token);
            return !mRefuseCancel;
        }
        std::vector<std::uint32_t> mLaunched;
        std::vector<std::uint32_t> mCancelled;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        bool mRefuseCancel = false;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    struct RuneGlyph : ::testing::Test
    {
        RuneData mData;
        SkyrimCombat mCombat;
        RuneWriter mWriter;

        void start()
        {
            mCombat.setSpellDelivery(true);
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(20, kEnemyBase, "Brenuin"));
            place(20, 2000.0, 0.0);
            mWriter.mPlayerState.heading = kPi / 2.f;
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void place(WorldId reference, double x, double y)
        {
            SubjectState s;
            s.x = x;
            s.y = y;
            s.heading = kPi / 2.f;
            mWriter.mStates[reference] = s;
        }
        void update(float seconds = 0.25f) { mCombat.combatUpdate(seconds, mWriter); }
        void updates(int n, float seconds = 0.25f)
        {
            for (int i = 0; i < n; ++i)
                update(seconds);
        }
        void cast()
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell("FireRune"));
            mCombat.onPlayerAttackEdge(true);
            updates(4);
            mCombat.onPlayerAttackEdge(false);
            update();
        }
        void castAndLand(double x)
        {
            cast();
            ASSERT_FALSE(mWriter.mLaunched.empty());
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = mWriter.mLaunched.back();
            r.fate = ProjectileFate::HitWorld;
            r.x = x;
            mCombat.onProjectileResult(r);
            update();
        }
    };

    TEST_F(RuneGlyph, aRuneThatGoesOffTakesItsGlyphAway)
    {
        start();
        castAndLand(500.0);
        ASSERT_EQ(mCombat.runesStanding(), 1u);
        const std::uint32_t token = mWriter.mLaunched.back();
        EXPECT_TRUE(mWriter.mCancelled.empty()) << "the glyph stays while the rune stands";
        place(20, 640.0, 0.0); // 140 units from the rune: inside the 150 trigger
        update();
        ASSERT_EQ(mCombat.runesSetOff(), 1u);
        ASSERT_EQ(mWriter.mCancelled.size(), 1u);
        EXPECT_EQ(mWriter.mCancelled[0], token);
    }

    TEST_F(RuneGlyph, aRuneThatEndsAfterItsLifetimeTakesItsGlyphAway)
    {
        start();
        castAndLand(500.0);
        const std::uint32_t token = mWriter.mLaunched.back();
        for (int i = 0; i < 14; ++i)
            update(50.f); // 700 seconds
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        ASSERT_EQ(mWriter.mCancelled.size(), 1u);
        EXPECT_EQ(mWriter.mCancelled[0], token);
    }

    TEST_F(RuneGlyph, aRuneThatFlewOutOfRangeHasNoGlyphToTakeAway)
    {
        start();
        cast();
        ASSERT_FALSE(mWriter.mLaunched.empty());
        ProjectileResult r;
        r.owner = CombatSubject::ofPlayer();
        r.token = mWriter.mLaunched.back();
        r.fate = ProjectileFate::Expired;
        mCombat.onProjectileResult(r);
        update();
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        EXPECT_TRUE(mWriter.mCancelled.empty());
    }

    TEST_F(RuneGlyph, theOldestRuneGoesWhenTheTableIsFullAndItsGlyphWithIt)
    {
        start();
        castAndLand(500.0);
        const std::uint32_t first = mWriter.mLaunched.back();
        for (std::size_t i = 1; i < kMaxRunes; ++i)
            castAndLand(500.0 + static_cast<double>(i));
        ASSERT_EQ(mCombat.runesStanding(), kMaxRunes);
        EXPECT_TRUE(mWriter.mCancelled.empty());
        castAndLand(900.0);
        EXPECT_EQ(mCombat.runesStanding(), kMaxRunes);
        ASSERT_EQ(mWriter.mCancelled.size(), 1u);
        EXPECT_EQ(mWriter.mCancelled[0], first);
    }

    TEST_F(RuneGlyph, aCoreThatRefusesTheCancelDoesNotStopTheRuneFromGoingOff)
    {
        start();
        mWriter.mRefuseCancel = true;
        castAndLand(500.0);
        place(20, 640.0, 0.0);
        update();
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        EXPECT_EQ(mCombat.runesSetOff(), 1u);
        EXPECT_EQ(mWriter.mCancelled.size(), 1u) << "asked once, not again every update";
        updates(3);
        EXPECT_EQ(mWriter.mCancelled.size(), 1u);
    }

    // ---- every wrapper passes the call on ---------------------------------------------------------------------

    class CrimeStubWorld final : public CrimeWorld
    {
    public:
        void actors(std::vector<CrimeActor>&) override {}
        bool actor(ActorHandle, CrimeActor&) override { return false; }
        const CrimeData* crimeData(FormId) override { return nullptr; }
        std::string factionName(FormId) override { return {}; }
        std::int32_t itemValue(FormId) override { return 0; }
        std::int32_t playerGold() override { return 0; }
        bool takeGold(std::int32_t) override { return false; }
        std::int32_t takeItem(FormId, std::int32_t) override { return 0; }
        std::int32_t itemCount(FormId) override { return 0; }
        void guardGoTo(ActorHandle, double, double, double, ActorSpeed) override {}
        void guardHold(ActorHandle) override {}
        void guardRelease(ActorHandle) override {}
        void guardAttack(ActorHandle) override {}
        void log(const std::string&) override {}
    };

    void expectForwards(CombatRequestWriter& wrapper, RuneWriter& inner)
    {
        inner.mRefuseCancel = false;
        EXPECT_TRUE(wrapper.cancelProjectile(41));
        ASSERT_EQ(inner.mCancelled.size(), 1u);
        EXPECT_EQ(inner.mCancelled[0], 41u);
        inner.mRefuseCancel = true;
        EXPECT_FALSE(wrapper.cancelProjectile(42));
        ASSERT_EQ(inner.mCancelled.size(), 2u);
        EXPECT_EQ(inner.mCancelled[1], 42u);
    }

    TEST(RuneGlyphWrappers, theDefaultWriterRefuses)
    {
        struct Bare final : CombatRequestWriter
        {
        } bare;
        EXPECT_FALSE(bare.cancelProjectile(1));
    }

    TEST(RuneGlyphWrappers, theHudWriterForwardsIt)
    {
        RuneWriter inner;
        CombatHud hud;
        HudWriter wrapper(inner, hud);
        expectForwards(wrapper, inner);
    }

    TEST(RuneGlyphWrappers, theAiDamageWriterForwardsIt)
    {
        RuneWriter inner;
        SkyrimCombat combat;
        AiDamageWriter wrapper(inner, combat);
        expectForwards(wrapper, inner);
    }

    TEST(RuneGlyphWrappers, theScriptEventWriterForwardsIt)
    {
        RuneWriter inner;
        ScriptCombatBridge bridge;
        ScriptEventWriter wrapper(inner, bridge);
        expectForwards(wrapper, inner);
    }

    TEST(RuneGlyphWrappers, theCrimeWriterForwardsIt)
    {
        RuneWriter inner;
        Ledger ledger;
        CrimeStubWorld world;
        CrimeSystem system(ledger, world);
        CrimeWriter wrapper(inner, system);
        expectForwards(wrapper, inner);
    }
}
