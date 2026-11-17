// Task 0191 (visible spells and beams): the pack's held aimed spells (Flames, Frostbite and Sparks work as a ray each
// frame), touch spells, the drawn-only stream of a beam and the ward body, through the combat contract 0.5 with a fake
// data source and a recording writer; and the ARTO / MGEF art decoders. Every number is derived by hand:
//  - Flames: cost 6 magicka a second, concentration, aimed, one damage effect of 8 health a second (fire resistance
//    41) with a flame projectile of speed 1000 and range 768;
//  - FrostLike: cost 6 a second, a Dual effect of 10 health and, by the weight 0.5, 5 stamina a second;
//  - Touchy: cost 20, fire and forget, touch, 20 health at once;
//  - WardLesser: as in testskyrimsecombatmagic.cpp, with a hit art on its effect.
// The player has 100 magicka and the victim 50 health. HOW the numbers combine is RECALLED-VERIFY: these tests pin
// THIS implementation.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/perks.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    constexpr WorldId kCasterBase = 100;
    constexpr WorldId kVictimBase = 200;
    constexpr FormId kFlames = 0x1000, kFrost = 0x1001, kTouch = 0x1002, kWard = 0x1003, kWardNoArt = 0x1004,
                     kAbilityResist = 0x4000;
    constexpr FormId kFlameEffect = 0x2000, kFrostEffect = 0x2001, kTouchEffect = 0x2002, kAccumulate = 0x2003,
                     kPeak = 0x2004, kPeakNoArt = 0x2005, kResistEffect = 0x4001;
    constexpr FormId kFlameProj = 0x3000, kWardArt = 0x5000;
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
            mProj.mCommon.mId = kFlameProj;
            mProj.mCommon.mEditorId = "FlamesProjectile";
            mProj.mHasData = true;
            mProj.mType = 8;
            mProj.mSpeed = 1000.f;
            mProj.mGravity = 0.f;
            mProj.mRange = 768.f;
            mProj.mModel = "Magic\\FireSprayFX01.nif";

            addEffect(kFlameEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental, kProjectileIfAny(), 41);
            addEffect(kFrostEffect, MagicArchetype::Dual, 24, MagicFlag::Hostile | MagicFlag::Detrimental,
                kProjectileIfAny());
            mEffects[kFrostEffect].mSecondaryValue = 26;
            mEffects[kFrostEffect].mSecondaryWeight = 0.5f;
            addEffect(kTouchEffect, 0, 24, MagicFlag::Hostile | MagicFlag::Detrimental);
            addEffect(kAccumulate, MagicArchetype::AccumulateMagnitude, 63, 0x40001002);
            addEffect(kPeak, MagicArchetype::PeakValueModifier, 39, 0x40001002);
            mEffects[kAccumulate].mHitEffectArt = kWardArt;
            addEffect(kPeakNoArt, MagicArchetype::PeakValueModifier, 39, 0x40001002);

            addSpell(
                kFlames, "Flames", 6, MagicCast::Concentration, MagicDelivery::Aimed, { { kFlameEffect, 8.f, 1, 0 } });
            addSpell(kFrost, "FrostLike", 6, MagicCast::Concentration, MagicDelivery::Aimed,
                { { kFrostEffect, 10.f, 1, 0 } });
            addSpell(
                kTouch, "Touchy", 20, MagicCast::FireAndForget, MagicDelivery::Touch, { { kTouchEffect, 20.f, 0, 0 } });
            addSpell(kWard, "WardLesser", 34, MagicCast::Concentration, MagicDelivery::Self,
                { { kAccumulate, 40.f, 0, 0 }, { kPeak, 40.f, 0, 0 } });
            addSpell(kWardNoArt, "WardNoArt", 34, MagicCast::Concentration, MagicDelivery::Self,
                { { kPeakNoArt, 40.f, 0, 0 } });
            mEffects[kPeakNoArt].mArchetype = MagicArchetype::AccumulateMagnitude;
            mEffects[kPeakNoArt].mHitEffectArt = kNullFormId;

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
        static FormId kProjectileIfAny() { return kFlameProj; }
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
        const ProjectileRecord* projectileById(FormId id) override { return id == kFlameProj ? &mProj : nullptr; }
        std::string_view artModelById(FormId id) override
        {
            return id == kWardArt ? std::string_view("Magic\\WardBodyFX.nif") : std::string_view();
        }
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
        struct Launch
        {
            CombatSubject mOwner;
            ProjectileRequest mRequest;
            std::string mModel;
        };
        struct Shown
        {
            CombatSubject mSubject;
            std::uint32_t mToken;
            float mHeight;
            std::string mModel;
        };
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            if (request.slot == ClipSlot::Cast)
                mCastClips.push_back({ subject, request.token });
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
            return mRayAccepts;
        }
        bool showEffect(CombatSubject subject, const EffectRequest& request) override
        {
            mShown.push_back({ subject, request.token, request.heightOffset, std::string(request.model) });
            return true;
        }
        bool hideEffect(std::uint32_t token) override
        {
            mHidden.push_back(token);
            return true;
        }

        std::vector<std::pair<CombatSubject, std::uint32_t>> mCastClips; ///< (caster, token) of each cast clip
        std::vector<Launch> mLaunches;
        std::vector<DamageReport> mDamage;
        std::vector<ActorHandle> mDeaths;
        std::map<WorldId, SubjectState> mStates;
        SubjectState mPlayerState;
        std::vector<std::pair<CombatSubject, RayRequest>> mRays;
        RayHit mRayHit;
        bool mRayAccepts = true;
        std::vector<Shown> mShown;
        std::vector<std::uint32_t> mHidden;
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
    struct CombatBeams : ::testing::Test
    {
        FakeData mData;
        SkyrimCombat mCombat;
        FakeWriter mWriter;
        ActorHandle mA{ 10, 1 };
        ActorHandle mB{ 20, 1 };
        std::vector<std::string> mTrace;
        std::size_t mDelivered = 0;

        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
        }
        void start(bool beams = true, bool visuals = false)
        {
            mCombat.setSpellBeams(beams);
            mCombat.setSpellVisuals(visuals);
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
            mWriter.mRayHit = hitOf(mB, 300.f);
        }
        static RayHit hitOf(ActorHandle actor, float distance)
        {
            RayHit hit;
            hit.fate = RayFate::Actor;
            hit.actor = actor;
            hit.x = distance;
            hit.distance = distance;
            return hit;
        }
        void update(float seconds = 0.25f)
        {
            // The core delivers the cast clip's SpellFire event; the test does it the update after the clip starts.
            for (std::size_t i = mDelivered; i < mWriter.mCastClips.size(); ++i)
            {
                ClipEvent event;
                event.subject = mWriter.mCastClips[i].first;
                event.slot = ClipSlot::Cast;
                event.token = mWriter.mCastClips[i].second;
                event.kind = ClipEventKind::SpellFire;
                mCombat.onClipEvent(event);
            }
            mDelivered = mWriter.mCastClips.size();
            mCombat.combatUpdate(seconds, mWriter);
        }
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
        SkyrimCombat::FighterView viewA() { return view(CombatSubject::of(mA)); }
        // A fighter the combat never touched is not registered yet: its health is its maximum.
        float healthOf(CombatSubject subject, float maximum)
        {
            SkyrimCombat::FighterView v;
            return mCombat.fighterView(subject, v) ? v.mHealth : maximum;
        }
        float healthA() { return healthOf(CombatSubject::of(mA), 100.f); }
        float healthB() { return healthOf(CombatSubject::of(mB), 50.f); }
        SkyrimCombat::FighterView viewB() { return view(CombatSubject::of(mB)); }
        SkyrimCombat::FighterView viewP() { return view(CombatSubject::ofPlayer()); }

        // The player holds `spell` for `n` updates of `seconds`, then lets go and runs one more update.
        void playerHolds(const char* spell, int n, float seconds = 0.25f)
        {
            ASSERT_TRUE(mCombat.setTestPlayerSpell(spell));
            mCombat.onPlayerAttackEdge(true);
            updates(n, seconds);
            mCombat.onPlayerAttackEdge(false);
            update(seconds);
        }
        float seconds() { return (100.f - viewP().mMagicka) / 6.f; } // the seconds the beam was paid for
    };

    // ---- the records ---------------------------------------------------------------------------------------------

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Fixture
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

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        return r;
    }

    TEST(SkyrimMagicArt, aMagicEffectKeepsItsCastingAndHitArt)
    {
        Fixture f;
        std::string why;
        Bytes mgef;
        mgef.append(sub4("EDID", Bytes().zstring("WardConcSelf0")));
        Bytes data;
        for (int i = 0; i < 38; ++i)
            data.u32(0);
        std::string text = data.str();
        auto putU = [&](std::size_t offset, std::uint32_t v) { std::memcpy(text.data() + offset, &v, 4); };
        putU(92, 0x00000888);
        putU(96, 0x00000999);
        Bytes dataBytes;
        dataBytes.text(text);
        mgef.append(sub4("DATA", dataBytes));
        MagicEffectRecord m;
        ASSERT_TRUE(decodeMagicEffect(view(mgef), raw("MGEF", 0x01000C20), f.context(), m, why)) << why;
        EXPECT_EQ(m.mCastingArt, *f.mResolver.resolve(0x888));
        EXPECT_EQ(m.mHitEffectArt, *f.mResolver.resolve(0x999));

        Bytes none;
        none.append(sub4("EDID", Bytes().zstring("NoArt")));
        Bytes zeros;
        for (int i = 0; i < 38; ++i)
            zeros.u32(0);
        none.append(sub4("DATA", zeros));
        MagicEffectRecord n;
        ASSERT_TRUE(decodeMagicEffect(view(none), raw("MGEF", 0x01000C21), f.context(), n, why)) << why;
        EXPECT_EQ(n.mCastingArt, kNullFormId);
        EXPECT_EQ(n.mHitEffectArt, kNullFormId);
    }

    TEST(SkyrimMagicArt, anArtObjectKeepsItsModel)
    {
        Fixture f;
        std::string why;
        Bytes arto;
        arto.append(sub4("EDID", Bytes().zstring("WardHitEffect")));
        arto.append(sub4("MODL", Bytes().zstring("Magic\\WardBodyFX.nif")));
        ArtRecord a;
        ASSERT_TRUE(decodeArt(view(arto), raw("ARTO", 0x01000D00), f.context(), a, why)) << why;
        EXPECT_EQ(a.mCommon.mEditorId, "WardHitEffect");
        EXPECT_EQ(a.mModel, "Magic\\WardBodyFX.nif");
    }

    TEST(SkyrimMagicArt, theIndexAnswersAnArtObjectByFormId)
    {
        CombatRecords records;
        ArtRecord a;
        a.mCommon.mId = 0x1234;
        a.mModel = "Magic\\WardBodyFX.nif";
        records.mArts.push_back(a);
        CombatIndex index;
        index.add(records);
        ASSERT_NE(index.art(0x1234), nullptr);
        EXPECT_EQ(index.art(0x1234)->mModel, "Magic\\WardBodyFX.nif");
        EXPECT_EQ(index.art(0x4321), nullptr);
    }

    // ---- held aimed spells (beams) -------------------------------------------------------------------------------

    TEST_F(CombatBeams, withTheSettingOffAHeldAimedSpellDoesNothingAsBefore)
    {
        start(false);
        playerHolds("Flames", 8);
        EXPECT_TRUE(mWriter.mRays.empty());
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 100.f);
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        EXPECT_TRUE(traceHas("not supported"));
    }

    TEST_F(CombatBeams, aHeldFlamesDoesEightDamageASecondForSixMagickaASecond)
    {
        start();
        playerHolds("Flames", 8);
        const float s = seconds();
        EXPECT_GT(s, 1.f);
        EXPECT_NEAR(50.f - healthB(), 8.f * s, 1.0e-3f);
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        EXPECT_FALSE(mWriter.mRays.empty());
        ASSERT_FALSE(mWriter.mDamage.empty());
        EXPECT_TRUE(mWriter.mDamage.back().attackerIsPlayer);
        EXPECT_FALSE(mWriter.mDamage.back().victimIsPlayer);
    }

    TEST_F(CombatBeams, aFireResistanceOf50PercentHalvesTheBeam)
    {
        mData.mUseLibrary = true;
        mData.mProfiles[kVictimBase].mSpells = { kAbilityResist };
        start();
        playerHolds("Flames", 8);
        EXPECT_GT(seconds(), 1.f);
        EXPECT_NEAR(50.f - healthB(), 4.f * seconds(), 1.0e-3f);
    }

    TEST_F(CombatBeams, aBeamThatMeetsNothingOrAWallHurtsNoOneButStillCostsMagicka)
    {
        start();
        mWriter.mRayHit = {};
        playerHolds("Flames", 8);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_GT(seconds(), 1.f);

        mWriter.mRayHit.fate = RayFate::World;
        mWriter.mRayHit.distance = 200.f;
        playerHolds("Flames", 8);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
    }

    TEST_F(CombatBeams, aDualEffectTakesTheSecondValueByItsWeight)
    {
        start();
        playerHolds("FrostLike", 8);
        const float s = seconds();
        EXPECT_NEAR(50.f - healthB(), 10.f * s, 1.0e-3f);
        EXPECT_NEAR(80.f - viewB().mStamina, 5.f * s, 1.0e-3f); // weight 0.5
    }

    TEST_F(CombatBeams, theLastTickIsScaledByTheMagickaThatWasLeft)
    {
        mData.mPlayer.mMagickaMax = 3.f; // half a second of Flames
        start();
        playerHolds("Flames", 12);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 0.f);
        EXPECT_NEAR(50.f - healthB(), 4.f, 1.0e-3f); // 8 a second x 0.5 s
        EXPECT_EQ(mCombat.concentrationsHeld(), 0u);
        EXPECT_TRUE(traceHas("out of magicka"));
    }

    TEST_F(CombatBeams, nothingHappensAfterTheRelease)
    {
        start();
        playerHolds("Flames", 8);
        const float health = healthB();
        EXPECT_LT(health, 50.f); // the beam did hurt before it was released
        const float magicka = viewP().mMagicka;
        const std::size_t rays = mWriter.mRays.size();
        updates(4);
        EXPECT_FLOAT_EQ(healthB(), health);
        EXPECT_EQ(mWriter.mRays.size(), rays);
        EXPECT_GE(viewP().mMagicka, magicka); // it only regenerates
    }

    TEST_F(CombatBeams, thePlayersRayStartsAtTheEyeAlongTheViewAndReachesTheProjectilesRange)
    {
        start();
        mWriter.mPlayerState.x = 0.0; // the player stands at the origin, facing +X, looking a little up
        mWriter.mPlayerState.pitch = 0.1f;
        playerHolds("Flames", 4);
        ASSERT_FALSE(mWriter.mRays.empty());
        const auto& [from, ray] = mWriter.mRays.back();
        EXPECT_TRUE(from.player);
        EXPECT_NEAR(ray.x, 30.0, 1.0e-3); // kBowMuzzleForward ahead along heading pi/2
        EXPECT_NEAR(ray.y, 0.0, 1.0e-3);
        EXPECT_NEAR(ray.z, 118.0, 1.0e-3); // kPlayerEyeHeight
        EXPECT_NEAR(ray.heading, kPi / 2.f, 1.0e-5f);
        EXPECT_NEAR(ray.pitch, 0.1f, 1.0e-5f);
        EXPECT_NEAR(ray.range, 768.f, 1.0e-3f); // the PROJ's range
    }

    TEST_F(CombatBeams, anActorHoldingFlamesAimsAtThePlayerAndHurtsHim)
    {
        start();
        mWriter.mRayHit.fate = RayFate::Player;
        mWriter.mRayHit.distance = 300.f;
        mCombat.setTestCasts("Archer:Flames");
        mCombat.setTestCastEvery(1.f);
        mCombat.setTestCastHold(2.f);
        updates(16);
        ASSERT_FALSE(mWriter.mRays.empty());
        const auto& [from, ray] = mWriter.mRays.front();
        EXPECT_FALSE(from.player);
        EXPECT_EQ(from.actor, mA);
        EXPECT_NEAR(ray.heading, kPi / 2.f, 1.0e-3f); // the player is east of him
        EXPECT_LT(std::abs(ray.pitch), 0.05f);
        const float lost = 100.f - mWriter.mPlayerState.health;
        EXPECT_GT(lost, 4.f);
        EXPECT_NEAR(lost, 8.f * (100.f - viewA().mMagicka) / 6.f, 0.05f);
    }

    TEST_F(CombatBeams, aBeamThatHitsItsOwnCasterDoesNothing)
    {
        start();
        mWriter.mRayHit = hitOf(mA, 5.f);
        mCombat.setTestCasts("Archer:Flames");
        mCombat.setTestCastEvery(1.f);
        mCombat.setTestCastHold(2.f);
        updates(16);
        EXPECT_FLOAT_EQ(healthA(), 100.f);
        EXPECT_TRUE(mWriter.mDamage.empty());
    }

    TEST_F(CombatBeams, aBeamHeldLongEnoughKillsAFiftyHealthVictimOnce)
    {
        start();
        playerHolds("Flames", 40, 0.25f); // 8 a second: dead after 6.25 s
        EXPECT_FLOAT_EQ(healthB(), 0.f);
        EXPECT_TRUE(viewB().mDead);
        ASSERT_EQ(mWriter.mDeaths.size(), 1u);
        EXPECT_EQ(mWriter.mDeaths[0], mB);
    }

    // ---- touch ---------------------------------------------------------------------------------------------------

    TEST_F(CombatBeams, aTouchSpellHitsWhatIsWithinReachAndPaysItsCost)
    {
        start();
        mWriter.mRayHit = hitOf(mB, 100.f);
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Touchy"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        mCombat.onPlayerAttackEdge(false);
        updates(2);
        ASSERT_FALSE(mWriter.mRays.empty());
        EXPECT_NEAR(mWriter.mRays.front().second.range, kTouchReach, 1.0e-3f);
        EXPECT_FLOAT_EQ(healthB(), 30.f); // 50 - 20
        EXPECT_FLOAT_EQ(viewP().mMagicka, 80.f); // cost 20
        EXPECT_EQ(mCombat.spellsCast(), 1u);
    }

    TEST_F(CombatBeams, aTouchSpellThatFindsNothingStillCostsMagickaAndHurtsNoOne)
    {
        start();
        mWriter.mRayHit = {};
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Touchy"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        mCombat.onPlayerAttackEdge(false);
        updates(2);
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_FLOAT_EQ(viewP().mMagicka, 80.f);
    }

    TEST_F(CombatBeams, withTheSettingOffATouchSpellIsNotSupportedAsBefore)
    {
        start(false);
        ASSERT_TRUE(mCombat.setTestPlayerSpell("Touchy"));
        mCombat.onPlayerAttackEdge(true);
        updates(4);
        mCombat.onPlayerAttackEdge(false);
        updates(2);
        EXPECT_TRUE(mWriter.mRays.empty());
        EXPECT_FLOAT_EQ(healthB(), 50.f);
        EXPECT_TRUE(traceHas("is not supported"));
    }

    // ---- what is drawn -------------------------------------------------------------------------------------------

    TEST_F(CombatBeams, aHeldBeamStreamsDrawnOnlyCopiesOfItsProjectileUpToTheHitPoint)
    {
        start(true, true);
        playerHolds("Flames", 40, 0.05f); // two seconds, 20 updates a second
        std::size_t visual = 0;
        for (const FakeWriter::Launch& launch : mWriter.mLaunches)
        {
            EXPECT_TRUE(launch.mRequest.visualOnly);
            EXPECT_TRUE(launch.mOwner.player);
            EXPECT_EQ(launch.mModel, "meshes/Magic/FireSprayFX01.nif");
            EXPECT_FLOAT_EQ(launch.mRequest.speed, 1000.f);
            EXPECT_NEAR(launch.mRequest.range, 300.f, 1.0e-3f); // up to the hit
            ++visual;
        }
        EXPECT_GE(visual, 15u);
        EXPECT_LE(visual, 40u);
    }

    TEST_F(CombatBeams, withTheVisualsOffABeamLaunchesNothing)
    {
        start(true, false);
        playerHolds("Flames", 40, 0.05f);
        EXPECT_TRUE(mWriter.mLaunches.empty());
        EXPECT_GT(50.f - healthB(), 4.f); // the beam still works
    }

    TEST_F(CombatBeams, aWardShowsItsHitArtWhileItIsUpAndHidesItWhenItEnds)
    {
        start(true, true);
        playerHolds("WardLesser", 8);
        update();
        ASSERT_EQ(mWriter.mShown.size(), 1u);
        const FakeWriter::Shown& shown = mWriter.mShown[0];
        EXPECT_TRUE(shown.mSubject.player);
        EXPECT_EQ(shown.mModel, "meshes/Magic/WardBodyFX.nif");
        EXPECT_FLOAT_EQ(shown.mHeight, kWardBodyHeight);
        ASSERT_EQ(mWriter.mHidden.size(), 1u);
        EXPECT_EQ(mWriter.mHidden[0], shown.mToken);
    }

    TEST_F(CombatBeams, aWardWithNoArtShowsNothing)
    {
        start(true, true);
        playerHolds("WardNoArt", 8);
        update();
        EXPECT_TRUE(mWriter.mShown.empty());
        EXPECT_TRUE(mWriter.mHidden.empty());
    }

    TEST_F(CombatBeams, aWardWithTheVisualsOffShowsNothing)
    {
        start(true, false);
        playerHolds("WardLesser", 8);
        update();
        EXPECT_TRUE(mWriter.mShown.empty());
        EXPECT_TRUE(mWriter.mHidden.empty());
    }

    TEST_F(CombatBeams, aWardThatIsDeadOrGoneHidesItsArt)
    {
        start(true, true);
        ASSERT_TRUE(mCombat.setTestPlayerSpell("WardLesser"));
        mCombat.onPlayerAttackEdge(true);
        updates(6);
        ASSERT_EQ(mWriter.mShown.size(), 1u);
        mCombat.onActorRemoved(mA); // some other actor leaves: the ward stays
        update();
        EXPECT_TRUE(mWriter.mHidden.empty());
        mCombat.onPlayerAttackEdge(false);
        updates(2);
        EXPECT_EQ(mWriter.mHidden.size(), 1u);
    }
}
