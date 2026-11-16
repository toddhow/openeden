// Task 0242: spell delivery (target actor, target location and area), the pack side, through the combat contract with a
// fake data source and a recording writer. Every number is derived by hand:
//  - Fireball: aimed, fire and forget, cost 20; one fire damage effect (health, hostile) of 30 with an area of 15 feet
//    (15 x 64/3 = 320 game units); a second effect, Burn, of 5 with no area (it goes to the direct victim only);
//  - HealOther: target actor, cost 15, restore health 20 at once; its projectile (the "fake" one) has range 3000;
//  - FireRune: target location, cost 20, one fire damage effect of 20 with no area; its projectile has an explosion
//    (an EXPL of radius 320 units) and an alternate trigger proximity of 150 units; it sticks where it lands.
// Brenuin (handle 20, base 200) is an enemy of the player, Cid (30, base 201) an ally, Dora (40, base 202) neutral;
// each has 50 health. Armour and resistances are none. HOW the rules combine is OURS or RECALLED-VERIFY: these tests
// pin THIS implementation. The decoders of EXPL and the PROJ additions are written to the layout read from
// Skyrim.esm (note in the task).
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/perks.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kCasterBase = 100, kEnemyBase = 200, kAllyBase = 201, kNeutralBase = 202;
    constexpr FormId kFireball = 0x1000, kHealOther = 0x1001, kFireRune = 0x1002, kPinprick = 0x1003;
    constexpr FormId kFireEffect = 0x2000, kBurnEffect = 0x2001, kHealEffect = 0x2002, kRuneEffect = 0x2003,
                     kPinEffect = 0x2004;
    constexpr FormId kFireballProj = 0x3000, kHealProj = 0x3001, kRuneProj = 0x3002, kRuneExpl = 0x4000,
                     kPinProj = 0x3003, kPinExpl = 0x4001;
    constexpr float kPi = 3.14159265358979f;
    constexpr float kFeet = 64.f / 3.f;

    class DeliveryData final : public CombatData
    {
    public:
        DeliveryData()
        {
            addProjectile(kFireballProj, 2500.f, 10000.f, "Magic\\FireBallProjectile.nif");
            addProjectile(kHealProj, 3000.f, 3000.f, "");
            addProjectile(kRuneProj, 1000.f, 10000.f, "Magic\\RuneFire.nif");
            mProjectiles[kRuneProj].mExplosion = kRuneExpl;
            mProjectiles[kRuneProj].mProximity = 150.f;
            mExplosion.mCommon.mId = kRuneExpl;
            mExplosion.mHasData = true;
            mExplosion.mRadius = 320.f;

            // A projectile that names an explosion of 1 unit (as the paralysis bolt's does): only the victim is in it.
            addProjectile(kPinProj, 2500.f, 10000.f, "Magic/Pin.nif");
            mProjectiles[kPinProj].mExplosion = kPinExpl;
            mPinExplosion.mCommon.mId = kPinExpl;
            mPinExplosion.mHasData = true;
            mPinExplosion.mRadius = 1.f;
            addEffect(kPinEffect, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kPinProj);
            addSpell(kPinprick, "Pinprick", 10, MagicDelivery::Aimed, { { kPinEffect, 7.f, 0 } });
            addEffect(kFireEffect, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kFireballProj);
            addEffect(kBurnEffect, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kFireballProj);
            addEffect(kHealEffect, 24, 0x201000, kHealProj);
            mEffects[kHealEffect].mDelivery = MagicDelivery::TargetActor;
            addEffect(kRuneEffect, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kRuneProj);
            mEffects[kRuneEffect].mDelivery = MagicDelivery::TargetLocation;

            addSpell(kFireball, "Fireball", 20, MagicDelivery::Aimed,
                { { kFireEffect, 30.f, 15 }, { kBurnEffect, 5.f, 0 } });
            addSpell(kHealOther, "HealOther", 15, MagicDelivery::TargetActor, { { kHealEffect, 20.f, 0 } });
            addSpell(kFireRune, "FireRune", 20, MagicDelivery::TargetLocation, { { kRuneEffect, 20.f, 0 } });

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
            victim.mAi.mRelationToPlayer = 2; // ally
            mProfiles[kAllyBase] = victim;
            victim.mAi.mRelationToPlayer = 0; // neutral
            mProfiles[kNeutralBase] = victim;

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
        const ProjectileRecord* projectileById(FormId id) override
        {
            const auto it = mProjectiles.find(id);
            return it == mProjectiles.end() ? nullptr : &it->second;
        }
        const ExplosionRecord* explosionById(FormId id) override
        {
            return id == kRuneExpl ? &mExplosion : id == kPinExpl ? &mPinExplosion : nullptr;
        }
        MagicSettings magicSettings() override { return {}; }

        struct E
        {
            FormId mEffect;
            float mMagnitude;
            std::uint32_t mArea;
        };
        void addProjectile(FormId id, float speed, float range, const char* model)
        {
            ProjectileRecord p;
            p.mCommon.mId = id;
            p.mCommon.mEditorId = "Proj" + std::to_string(id);
            p.mHasData = true;
            p.mType = 1;
            p.mSpeed = speed;
            p.mRange = range;
            p.mModel = model;
            mProjectiles[id] = p;
        }
        void addEffect(FormId id, std::int32_t value, std::uint32_t flags, FormId projectile)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Effect" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = 0;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            m.mProjectile = projectile;
            mEffects[id] = m;
        }
        void addSpell(FormId id, const char* name, std::uint32_t cost, std::uint32_t delivery,
            const std::vector<E>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = name;
            s.mHasData = true;
            s.mCost = cost;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = delivery;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                se.mArea = e.mArea;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }

        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        std::map<FormId, ProjectileRecord> mProjectiles;
        ExplosionRecord mExplosion;
        ExplosionRecord mPinExplosion;
        std::map<WorldId, FighterProfile> mProfiles;
        FighterProfile mPlayer;
    };

    class DeliveryWriter final : public CombatRequestWriter
    {
    public:
        struct Launch
        {
            CombatSubject mOwner;
            ProjectileRequest mRequest;
            std::string mModel;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mClips.push_back({ subject, request.token });
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
        bool requestSweep(CombatSubject, const SweepRequest&) override { return true; }
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
        bool castRay(CombatSubject from, const RayRequest& request, RayHit& out) override
        {
            mRays.push_back({ from, request });
            out = mRayHit;
            return true;
        }
        std::vector<std::pair<CombatSubject, std::uint32_t>> mClips; ///< (caster, token) of each cast clip
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        std::vector<std::pair<CombatSubject, RayRequest>> mRays;
        RayHit mRayHit;
    };

    ActorInfo info(WorldId reference, WorldId base, std::string_view editorId)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = editorId;
        return i;
    }

    // The player (the caster) at the origin facing +X; Brenuin (20) at x 800, Cid (30) at x 900 y 50 (112 units from
    // Brenuin), Dora (40) at x 1300 (500 units from Brenuin). Archer (10) is an NPC at the origin.
    struct SpellDeliveryTest : ::testing::Test
    {
        DeliveryData mData;
        SkyrimCombat mCombat;
        DeliveryWriter mWriter;
        ActorHandle mB{ 20, 1 };
        ActorHandle mC{ 30, 1 };
        ActorHandle mD{ 40, 1 };
        std::vector<std::string> mTrace;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start(bool delivery = true)
        {
            mCombat.setSpellDelivery(delivery);
            mCombat.setData(&mData);
            mCombat.onActorAdded(info(20, kEnemyBase, "Brenuin"));
            mCombat.onActorAdded(info(30, kAllyBase, "Cid"));
            mCombat.onActorAdded(info(40, kNeutralBase, "Dora"));
            place(20, 800.0, 0.0);
            place(30, 900.0, 50.0);
            place(40, 1300.0, 0.0);
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
        float healthOf(CombatSubject subject, float maximum)
        {
            SkyrimCombat::FighterView v;
            return mCombat.fighterView(subject, v) ? v.mHealth : maximum;
        }
        float healthB() { return healthOf(CombatSubject::of(mB), 50.f); }
        float healthC() { return healthOf(CombatSubject::of(mC), 50.f); }
        float healthD() { return healthOf(CombatSubject::of(mD), 50.f); }
        SkyrimCombat::FighterView viewP() { return view(CombatSubject::ofPlayer()); }

        // The player presses the attack control with `spell` selected and the cast goes off; returns the launch count
        // so far (a launch is the projectile of an aimed or location spell).
        void playerCasts(const char* spell)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            mCombat.onPlayerAttackEdge(true);
            updates(4); // the cast clip's time passes and the spell goes
            mCombat.onPlayerAttackEdge(false);
            update();
        }
        std::uint32_t lastToken() const
        {
            return mWriter.mLaunches.empty() ? 0 : mWriter.mLaunches.back().mRequest.token;
        }
        void ends(std::uint32_t token, ProjectileFate fate, double x, double y, double z, ActorHandle actor = {})
        {
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = token;
            r.fate = fate;
            r.actor = actor;
            r.x = x;
            r.y = y;
            r.z = z;
            mCombat.onProjectileResult(r);
            update();
        }
        std::size_t damageReportsOn(bool player) const
        {
            return static_cast<std::size_t>(std::count_if(mWriter.mDamage.begin(), mWriter.mDamage.end(),
                [&](const DamageReport& d) { return d.victimIsPlayer == player; }));
        }
    };

    // ---- the pure rules ---------------------------------------------------------------------------------------

    TEST(SpellDeliveryRules, selfAndAimedAreAlwaysCastAndTheOthersNeedTheirSettings)
    {
        EXPECT_TRUE(deliveryIsCast(MagicDelivery::Self, false, false));
        EXPECT_TRUE(deliveryIsCast(MagicDelivery::Aimed, false, false));
        EXPECT_FALSE(deliveryIsCast(MagicDelivery::Touch, false, false));
        EXPECT_TRUE(deliveryIsCast(MagicDelivery::Touch, true, false));
        EXPECT_FALSE(deliveryIsCast(MagicDelivery::TargetActor, true, false));
        EXPECT_TRUE(deliveryIsCast(MagicDelivery::TargetActor, false, true));
        EXPECT_FALSE(deliveryIsCast(MagicDelivery::TargetLocation, true, false));
        EXPECT_TRUE(deliveryIsCast(MagicDelivery::TargetLocation, false, true));
        EXPECT_FALSE(deliveryIsCast(9, true, true));
    }

    // ---- the records ---------------------------------------------------------------------------------------------

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct DecodeHarness
    {
        LoadOrder mOrder = oneMaster();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> bytesOf(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord rawOf(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        return r;
    }

    // EXPL DATA as Skyrim.esm stores it: six form words, then force, damage, radius, image space radius (+24 to +36).
    Bytes explData(float force, float damage, float radius, float impact, std::size_t size = 52)
    {
        if (size < 40)
        {
            Bytes shorter;
            for (std::size_t i = 0; i < size / 4; ++i)
                shorter.u32(0);
            return shorter;
        }
        Bytes b;
        for (int i = 0; i < 6; ++i)
            b.u32(0);
        b.f32(force).f32(damage).f32(radius).f32(impact);
        while (b.str().size() < size)
            b.u32(0);
        return b;
    }

    TEST(SpellDeliveryRecords, anExplosionKeepsItsRadiusAndForce)
    {
        DecodeHarness h;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("ExplosionRuneFlame01")));
        subs.append(sub4("DATA", explData(240.f, 2.f, 320.f, 1024.f)));
        ExplosionRecord x;
        std::string why;
        ASSERT_TRUE(decodeExplosion(bytesOf(subs), rawOf("EXPL", 0x01000E10), h.context(), x, why)) << why;
        EXPECT_EQ(x.mCommon.mEditorId, "ExplosionRuneFlame01");
        EXPECT_TRUE(x.mHasData);
        EXPECT_FLOAT_EQ(x.mForce, 240.f);
        EXPECT_FLOAT_EQ(x.mDamage, 2.f);
        EXPECT_FLOAT_EQ(x.mRadius, 320.f);
        EXPECT_FLOAT_EQ(x.mImpactRadius, 1024.f);
    }

    TEST(SpellDeliveryRecords, theShorterExplosionBlocksStillGiveTheRadiusAndAnOddOneGivesNothing)
    {
        DecodeHarness h;
        std::string why;
        Bytes shortBlock;
        shortBlock.append(sub4("DATA", explData(1.f, 0.f, 106.f, 600.f, 40)));
        ExplosionRecord a;
        ASSERT_TRUE(decodeExplosion(bytesOf(shortBlock), rawOf("EXPL", 0x01000E11), h.context(), a, why)) << why;
        EXPECT_TRUE(a.mHasData);
        EXPECT_FLOAT_EQ(a.mRadius, 106.f);

        Bytes tooShort;
        tooShort.append(sub4("DATA", explData(1.f, 0.f, 106.f, 600.f, 36)));
        ExplosionRecord b;
        ASSERT_TRUE(decodeExplosion(bytesOf(tooShort), rawOf("EXPL", 0x01000E12), h.context(), b, why)) << why;
        EXPECT_FALSE(b.mHasData);

        Bytes negative;
        negative.append(sub4("DATA", explData(1.f, 0.f, -5.f, 600.f)));
        ExplosionRecord c;
        ASSERT_TRUE(decodeExplosion(bytesOf(negative), rawOf("EXPL", 0x01000E13), h.context(), c, why)) << why;
        EXPECT_FALSE(c.mHasData);
        EXPECT_EQ(h.mNotes.mOddSubrecords, 2u);
    }

    // PROJ DATA 92 bytes: flags u16, type u16, gravity, speed, range (+4, +8, +12), light, muzzle light, tracer chance,
    // alternate trigger proximity +28, alternate timer +32, explosion +36.
    Bytes projWithExplosion(std::uint32_t explosion, float proximity)
    {
        Bytes b;
        b.u16(6).u16(2).f32(0.f).f32(1000.f).f32(10000.f);
        b.u32(0).u32(0).f32(0.f); // +16, +20, +24
        b.f32(proximity).f32(0.f).u32(explosion);
        while (b.str().size() < 92)
            b.u32(0);
        return b;
    }

    TEST(SpellDeliveryRecords, aProjectileKeepsItsExplosionAndProximity)
    {
        DecodeHarness h;
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("RuneFireProjectile")));
        subs.append(sub4("DATA", projWithExplosion(0x00000E10, 150.f)));
        ProjectileRecord p;
        std::string why;
        ASSERT_TRUE(decodeProjectile(bytesOf(subs), rawOf("PROJ", 0x01000D10), h.context(), p, why)) << why;
        EXPECT_FLOAT_EQ(p.mProximity, 150.f);
        EXPECT_EQ(p.mExplosion, *h.mResolver.resolve(0x00000E10));
        EXPECT_FLOAT_EQ(p.mSpeed, 1000.f);

        Bytes none;
        none.append(sub4("EDID", Bytes().zstring("Plain")));
        none.append(sub4("DATA", projWithExplosion(0, 0.f)));
        ProjectileRecord q;
        ASSERT_TRUE(decodeProjectile(bytesOf(none), rawOf("PROJ", 0x01000D11), h.context(), q, why)) << why;
        EXPECT_EQ(q.mExplosion, 0u);
        EXPECT_FLOAT_EQ(q.mProximity, 0.f);
    }

    TEST(SpellDeliveryRecords, theIndexAnswersAnExplosionByFormId)
    {
        CombatRecords records;
        ExplosionRecord x;
        x.mCommon.mId = 0x1234;
        x.mHasData = true;
        x.mRadius = 320.f;
        records.mExplosions.push_back(x);
        CombatIndex index;
        index.add(records);
        ASSERT_NE(index.explosion(0x1234), nullptr);
        EXPECT_FLOAT_EQ(index.explosion(0x1234)->mRadius, 320.f);
        EXPECT_EQ(index.explosion(0x4321), nullptr);
    }

    // ---- area: the Fireball --------------------------------------------------------------------------------------

    TEST_F(SpellDeliveryTest, withTheSettingOffAFireballOnTheWorldHurtsNobodyAsBefore)
    {
        start(false);
        playerCasts("Fireball");
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_FLOAT_EQ(healthC(), 50.f);
        EXPECT_EQ(mCombat.explosions(), 0u);
    }

    TEST_F(SpellDeliveryTest, withTheSettingOffADirectHitStillHurtsOnlyTheVictim)
    {
        start(false);
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitActor, 800.0, 0.0, 0.0, mB);
        EXPECT_FLOAT_EQ(healthB(), 15.f); // 50 - 30 - 5
        EXPECT_FLOAT_EQ(healthC(), 50.f);
    }

    TEST_F(SpellDeliveryTest, aFireballThatLandsOnTheWorldHurtsEveryoneWithinFifteenFeetOfWhereItEnded)
    {
        start();
        playerCasts("Fireball");
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 80.f);
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_FLOAT_EQ(healthB(), 20.f); // 30 from the area
        EXPECT_FLOAT_EQ(healthC(), 20.f); // 112 units away, inside 320
        EXPECT_FLOAT_EQ(healthD(), 50.f); // 500 units away, outside
        EXPECT_EQ(mCombat.explosions(), 1u);
        EXPECT_TRUE(traceHas("COMBAT explosion Fireball"));
    }

    TEST_F(SpellDeliveryTest, theAreaCoversTheDirectVictimOnceAndTheOthersAroundIt)
    {
        start();
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitActor, 800.0, 0.0, 0.0, mB);
        EXPECT_FLOAT_EQ(healthB(), 15.f); // the area 30 once, plus the 5 that has no area, once
        EXPECT_FLOAT_EQ(healthC(), 20.f); // the area only
        EXPECT_FLOAT_EQ(healthD(), 50.f);
        EXPECT_EQ(damageReportsOn(false), 3u); // B twice (two effects), C once
    }

    TEST_F(SpellDeliveryTest, anEffectWithNoAreaGoesToTheDirectVictimOnly)
    {
        start();
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        // Burn (5, no area) has nobody to go to when the shot hit the world: B and C lost only the 30.
        EXPECT_FLOAT_EQ(healthB(), 20.f);
        EXPECT_FLOAT_EQ(healthC(), 20.f);
    }

    TEST_F(SpellDeliveryTest, theCasterIsNeverHurtByHisOwnExplosion)
    {
        start();
        mWriter.mPlayerState.x = 700.0; // 100 units from where it will end
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_EQ(damageReportsOn(true), 0u);
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 100.f);
        EXPECT_FLOAT_EQ(healthB(), 20.f);
    }

    TEST_F(SpellDeliveryTest, theDamageHasNoFalloffInsideTheRadiusBecauseTheDataHasNone)
    {
        start();
        place(30, 800.0 + 300.0, 0.0); // 300 units of 320
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_FLOAT_EQ(healthB(), healthC());
    }

    TEST_F(SpellDeliveryTest, aTargetJustOutsideTheRadiusIsNotHurtAndOneJustInsideIs)
    {
        start();
        place(30, 800.0 + 319.0, 0.0);
        place(40, 800.0 + 321.0, 0.0);
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_FLOAT_EQ(healthC(), 20.f);
        EXPECT_FLOAT_EQ(healthD(), 50.f);
    }

    TEST_F(SpellDeliveryTest, theActorAShotHitIsAlwaysInsideItsExplosionEvenWhenTheRadiusIsTinyAndOthersAreNot)
    {
        start();
        playerCasts("Pinprick");
        ends(lastToken(), ProjectileFate::HitActor, 800.0, 0.0, 120.0, mB); // the impact is on the body, 120 units up
        EXPECT_FLOAT_EQ(healthB(), 43.f); // 7
        EXPECT_FLOAT_EQ(healthC(), 50.f); // 112 units away: outside a radius of 1
        EXPECT_EQ(mCombat.explosions(), 1u);
    }

    TEST_F(SpellDeliveryTest, aShotThatRanOutOfRangeLeavesNoExplosion)
    {
        start();
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::Expired, 800.0, 0.0, 0.0);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_EQ(mCombat.explosions(), 0u);
    }

    TEST_F(SpellDeliveryTest, aDeadActorInTheRadiusIsSkipped)
    {
        start();
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0); // B and C at 20
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0); // 20 -> dead? no: 20 - 30
        ASSERT_FALSE(mWriter.mDeaths.empty());
        const std::size_t deaths = mWriter.mDeaths.size();
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitWorld, 800.0, 0.0, 0.0);
        EXPECT_EQ(mWriter.mDeaths.size(), deaths) << "a corpse is not killed again";
    }

    // ---- target actor: Heal Other ------------------------------------------------------------------------------

    TEST_F(SpellDeliveryTest, withTheSettingOffATargetActorSpellIsRefusedAsBefore)
    {
        start(false);
        playerCasts("HealOther");
        EXPECT_TRUE(mWriter.mRays.empty());
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_TRUE(traceHas("is not supported"));
    }

    TEST_F(SpellDeliveryTest, aTargetActorSpellGoesToTheActorUnderTheCrosshair)
    {
        start();
        // B is hurt first (30 health), then Heal Other restores 20 of it.
        playerCasts("Fireball");
        ends(lastToken(), ProjectileFate::HitActor, 800.0, 0.0, 0.0, mB);
        ASSERT_FLOAT_EQ(healthB(), 15.f);
        mWriter.mRayHit = {};
        mWriter.mRayHit.fate = RayFate::Actor;
        mWriter.mRayHit.actor = mB;
        mWriter.mRayHit.x = 800.0;
        mWriter.mRayHit.distance = 800.f;
        playerCasts("HealOther");
        ASSERT_EQ(mWriter.mRays.size(), 1u);
        EXPECT_FLOAT_EQ(mWriter.mRays[0].second.range, 3000.f); // the range of the spell's projectile
        EXPECT_NEAR(mWriter.mRays[0].second.heading, kPi / 2.f, 1e-4f);
        EXPECT_TRUE(mWriter.mRays[0].first.player);
        EXPECT_FLOAT_EQ(healthB(), 35.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 65.f); // 100 - 20 - 15
        EXPECT_EQ(mWriter.mLaunches.size(), 1u) << "no projectile for a target actor spell";
    }

    TEST_F(SpellDeliveryTest, aTargetActorSpellWithNobodyInTheSightsDoesNothingAndCostsTheMagicka)
    {
        start();
        mWriter.mRayHit = {};
        mWriter.mRayHit.fate = RayFate::World;
        mWriter.mRayHit.x = 500.0;
        mWriter.mRayHit.distance = 500.f;
        playerCasts("HealOther");
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 85.f);
        EXPECT_TRUE(traceHas("found no actor"));
    }

    TEST_F(SpellDeliveryTest, aTargetActorSpellCannotBeCastOnTheCaster)
    {
        start();
        mWriter.mRayHit = {};
        mWriter.mRayHit.fate = RayFate::Player;
        playerCasts("HealOther");
        EXPECT_TRUE(traceHas("found no actor"));
    }

    // ---- target location: the runes ------------------------------------------------------------------------------

    TEST_F(SpellDeliveryTest, withTheSettingOffARuneIsRefusedAsBefore)
    {
        start(false);
        playerCasts("FireRune");
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_EQ(mCombat.runesStanding(), 0u);
    }

    TEST_F(SpellDeliveryTest, aRuneFliesAsAProjectileThatStaysWhereItLandsAndIsPlacedThere)
    {
        start();
        playerCasts("FireRune");
        ASSERT_EQ(mWriter.mLaunches.size(), 1u);
        const ProjectileRequest& r = mWriter.mLaunches[0].mRequest;
        EXPECT_FLOAT_EQ(r.speed, 1000.f);
        EXPECT_FLOAT_EQ(r.stickSeconds, kRuneLifetimeSeconds);
        EXPECT_GT(r.stickDepth, 0.f);
        EXPECT_NE(mWriter.mLaunches[0].mModel.find("RuneFire"), std::string::npos);
        EXPECT_EQ(mCombat.runesStanding(), 0u) << "not placed before the projectile lands";
        ends(lastToken(), ProjectileFate::HitWorld, 500.0, 0.0, 0.0);
        EXPECT_EQ(mCombat.runesStanding(), 1u);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 80.f);
        EXPECT_TRUE(traceHas("COMBAT rune FireRune placed"));
    }

    TEST_F(SpellDeliveryTest, aRuneThatFlewOutOfRangeIsNotPlaced)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::Expired, 500.0, 0.0, 0.0);
        EXPECT_EQ(mCombat.runesStanding(), 0u);
    }

    TEST_F(SpellDeliveryTest, anEnemyWithinTheTriggerDistanceSetsTheRuneOffAndTheExplosionHurtsEveryoneNear)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::HitWorld, 500.0, 0.0, 0.0);
        ASSERT_EQ(mCombat.runesStanding(), 1u);
        place(30, 700.0, 0.0); // the ally, 200 units from the rune: inside the explosion, outside the trigger
        place(20, 900.0, 0.0); // Brenuin, 400 units: outside both
        updates(4);
        EXPECT_EQ(mCombat.runesStanding(), 1u);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        place(20, 640.0, 0.0); // 140 units from the rune: inside the 150 trigger
        update();
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        EXPECT_EQ(mCombat.runesSetOff(), 1u);
        EXPECT_FLOAT_EQ(healthB(), 30.f); // the effect's 20: it has no area, so the explosion's 320 units apply
        EXPECT_FLOAT_EQ(healthC(), 30.f); // the ally is inside the 320
        EXPECT_FLOAT_EQ(healthD(), 50.f); // 800 units away
        EXPECT_TRUE(traceHas("COMBAT rune FireRune set off"));
        EXPECT_EQ(mCombat.explosions(), 1u);
    }

    TEST_F(SpellDeliveryTest, theTriggerDistanceIsTheProjectilesProximity)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::HitWorld, 500.0, 0.0, 0.0);
        place(20, 500.0 + 151.0, 0.0);
        updates(2);
        EXPECT_EQ(mCombat.runesStanding(), 1u);
        place(20, 500.0 + 149.0, 0.0);
        update();
        EXPECT_EQ(mCombat.runesStanding(), 0u);
    }

    TEST_F(SpellDeliveryTest, anAllyOrANeutralActorDoesNotSetARuneOff)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::HitWorld, 900.0, 0.0, 0.0);
        place(30, 900.0, 10.0); // the ally stands on it
        place(40, 880.0, 0.0); // the neutral one too
        place(20, 2000.0, 0.0);
        updates(4);
        EXPECT_EQ(mCombat.runesStanding(), 1u);
        EXPECT_EQ(mCombat.runesSetOff(), 0u);
    }

    TEST_F(SpellDeliveryTest, theCasterDoesNotSetHisOwnRuneOff)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::HitWorld, 0.0, 0.0, 0.0);
        place(20, 2000.0, 0.0);
        updates(4);
        EXPECT_EQ(mCombat.runesStanding(), 1u);
        EXPECT_EQ(damageReportsOn(true), 0u);
    }

    TEST_F(SpellDeliveryTest, aRuneEndsAfterItsLifetimeWithoutAnExplosion)
    {
        start();
        playerCasts("FireRune");
        ends(lastToken(), ProjectileFate::HitWorld, 500.0, 0.0, 0.0);
        place(20, 2000.0, 0.0);
        for (int i = 0; i < 14; ++i)
            update(50.f); // 700 seconds
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        EXPECT_EQ(mCombat.runesSetOff(), 0u);
        EXPECT_TRUE(traceHas("COMBAT rune FireRune expired"));
        EXPECT_FLOAT_EQ(healthB(), 50.f);
    }

    // Brenuin casts the rune (its clip's SpellFire event fires the cast) and it flies and lands; returns true when the
    // rune stands.
    bool brenuinPlacesARune(SpellDeliveryTest& t)
    {
        EXPECT_TRUE(t.mCombat.setTestCasts("Brenuin:FireRune"));
        t.mCombat.setTestCastEvery(1.f);
        t.updates(6);
        if (t.mWriter.mClips.empty())
            return false;
        ClipEvent event;
        event.subject = t.mWriter.mClips.back().first;
        event.slot = ClipSlot::Cast;
        event.token = t.mWriter.mClips.back().second;
        event.kind = ClipEventKind::SpellFire;
        t.mCombat.onClipEvent(event);
        t.update();
        if (t.mWriter.mLaunches.size() != 1)
            return false;
        t.mCombat.setTestCastEvery(0.f);
        ProjectileResult r;
        r.owner = t.mWriter.mLaunches[0].mOwner;
        r.token = t.mWriter.mLaunches[0].mRequest.token;
        r.fate = ProjectileFate::HitWorld;
        r.x = 100.0;
        t.mCombat.onProjectileResult(r);
        t.update();
        return t.mCombat.runesStanding() == 1u;
    }

    TEST_F(SpellDeliveryTest, anActorsRuneIsPlacedAndSetOffByTheEnemyItIsFor)
    {
        start();
        mWriter.mPlayerState.x = 0.0;
        ASSERT_TRUE(brenuinPlacesARune(*this));
        EXPECT_FALSE(mWriter.mLaunches[0].mOwner.player);
        EXPECT_FLOAT_EQ(mWriter.mLaunches[0].mRequest.stickSeconds, kRuneLifetimeSeconds);
        mWriter.mPlayerState.x = 200.0; // 100 units from the rune: the player is Brenuin's enemy
        update();
        EXPECT_EQ(mCombat.runesStanding(), 0u);
        EXPECT_EQ(damageReportsOn(true), 1u);
        EXPECT_FLOAT_EQ(mWriter.mPlayerState.health, 80.f);
    }

    TEST_F(SpellDeliveryTest, aRuneIsGoneWhenItsCasterIsRemoved)
    {
        start();
        ASSERT_TRUE(brenuinPlacesARune(*this));
        mCombat.onActorRemoved(mB);
        EXPECT_EQ(mCombat.runesStanding(), 0u);
    }

    TEST_F(SpellDeliveryTest, thirtyThreeRunesLeaveTheNewestThirtyTwo)
    {
        mData.mPlayer.mMagickaMax = 100000.f;
        start();
        for (int i = 0; i < 33; ++i)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell("FireRune"));
            mCombat.onPlayerAttackEdge(true);
            updates(4);
            mCombat.onPlayerAttackEdge(false);
            update();
            ends(lastToken(), ProjectileFate::HitWorld, 100.0 + i, 0.0, 0.0);
        }
        EXPECT_EQ(mCombat.runesStanding(), kMaxRunes);
        EXPECT_TRUE(traceHas("COMBAT rune FireRune replaced the oldest"));
    }

    std::vector<std::string> deliveryRun()
    {
        DeliveryData data;
        SkyrimCombat combat;
        DeliveryWriter writer;
        std::vector<std::string> trace;
        combat.setTrace([&](const std::string& line) { trace.push_back(line); });
        combat.setSpellDelivery(true);
        combat.setData(&data);
        combat.onActorAdded(info(20, kEnemyBase, "Brenuin"));
        combat.onActorAdded(info(30, kAllyBase, "Cid"));
        SubjectState b;
        b.x = 800.0;
        writer.mStates[20] = b;
        SubjectState c;
        c.x = 900.0;
        c.y = 50.0;
        writer.mStates[30] = c;
        writer.mPlayerState.heading = kPi / 2.f;
        writer.mPlayerState.healthMax = 100.f;
        writer.mPlayerState.health = 100.f;
        const auto cast = [&](const char* spell, double x) {
            EXPECT_TRUE(combat.setTestPlayerSpell(spell));
            combat.onPlayerAttackEdge(true);
            for (int i = 0; i < 4; ++i)
                combat.combatUpdate(0.25f, writer);
            combat.onPlayerAttackEdge(false);
            combat.combatUpdate(0.25f, writer);
            ProjectileResult r;
            r.owner = CombatSubject::ofPlayer();
            r.token = writer.mLaunches.back().mRequest.token;
            r.fate = ProjectileFate::HitWorld;
            r.x = x;
            combat.onProjectileResult(r);
            combat.combatUpdate(0.25f, writer);
        };
        cast("Fireball", 800.0);
        cast("FireRune", 1000.0);
        SubjectState nearRune;
        nearRune.x = 1050.0;
        writer.mStates[20] = nearRune;
        for (int i = 0; i < 2; ++i)
            combat.combatUpdate(0.25f, writer);
        return trace;
    }

    TEST(SpellDeliveryDeterminism, runesAndExplosionsGiveTheSameTraceInThreeRuns)
    {
        const std::vector<std::string> first = deliveryRun();
        EXPECT_FALSE(first.empty());
        EXPECT_EQ(deliveryRun(), first);
        EXPECT_EQ(deliveryRun(), first);
    }
}
